// SPDX-License-Identifier: MIT
#include "WTP-Client/include/wtp/session.hpp"
#include "wtp_integration/usb_cdc.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <deque>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <poll.h>
#include <stdexcept>
#include <sys/stat.h>
#ifdef __linux__
#include <sys/sysmacros.h>
#endif
#include <termios.h>
#include <unistd.h>

using namespace wsprrypi;
using wtp::IoState;
namespace {
unsigned checks{};
#define CHECK(x)                                                               \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(x))                                                                  \
      throw std::runtime_error("line " + std::to_string(__LINE__) + ": " #x);  \
  } while (false)
PicoCdcSelection selection() {
  return {"/dev/serial/by-id/test-wtp", "PICO123", 0xcafe, 0x4012};
}
CdcIdentity identity() {
  return {"/dev/ttyACM9",
          "/sys/devices/usb/board:1.2",
          "PICO123",
          99,
          0xcafe,
          0x4012,
          2};
}

struct Fake : CdcSystem {
  CdcIdentity device = identity();
  std::vector<std::string> calls;
  std::string fail_on;
  int closes{}, open_flags{}, next_fd{5}, close_error{};
  CdcPoll poll_result{1, POLLIN | POLLOUT, 0};
  CdcTransfer read_result{1, 0}, write_result{1, 0};
  bool automatic_io{true};
  std::size_t write_limit{4096}, read_limit{4096};
  std::deque<std::uint8_t> incoming;
  std::vector<std::uint8_t> outgoing;
  void call(std::string op) {
    calls.push_back(op);
    if (op == fail_on)
      throw std::runtime_error("injected " + op);
  }
  CdcIdentity inspect(const std::string &) override {
    call("inspect");
    return device;
  }
  int open(const std::string &path, int flags) override {
    call("open");
    CHECK(path == device.path);
    open_flags = flags;
    return next_fd;
  }
  void verify(int, const CdcIdentity &i) override {
    call("verify");
    if (i != device)
      throw std::runtime_error("changed identity");
  }
  void exclusive(int) override { call("exclusive"); }
  void unexclusive(int) override { call("unexclusive"); }
  void raw(int) override { call("raw"); }
  void dtr(int, bool on) override { call(on ? "dtr-on" : "dtr-off"); }
  void discard(int) override {
    call("discard");
    incoming.clear();
    outgoing.clear();
  }
  CdcPoll poll(int, bool) override {
    call("poll");
    return poll_result;
  }
  CdcTransfer read(int, std::span<std::uint8_t> bytes) override {
    call("read");
    CHECK(bytes.size() <= 4096);
    if (!automatic_io)
      return read_result;
    const auto count = std::min({bytes.size(), incoming.size(), read_limit});
    for (std::size_t i = 0; i < count; ++i) {
      bytes[i] = incoming.front();
      incoming.pop_front();
    }
    return count ? CdcTransfer{static_cast<std::ptrdiff_t>(count), 0}
                 : CdcTransfer{-1, EAGAIN};
  }
  CdcTransfer write(int, std::span<const std::uint8_t> bytes) override {
    call("write");
    CHECK(bytes.size() <= 4096);
    if (!automatic_io)
      return write_result;
    const auto count = std::min(bytes.size(), write_limit);
    outgoing.insert(outgoing.end(), bytes.begin(), bytes.begin() + count);
    return {static_cast<std::ptrdiff_t>(count), 0};
  }
  int close(int) noexcept override {
    ++closes;
    return close_error;
  }
};
void open_ready(UsbCdcStream &stream, std::uint64_t now = 0) {
  CHECK(stream.begin_open(selection(), now));
  CHECK(stream.state() == CdcState::Resetting);
  stream.poll_open(now);
  stream.poll_open(now + 100);
  CHECK(stream.state() == CdcState::Ready);
}

void setup_and_identity() {
  for (unsigned role : {0U, 1U, 3U, 999U}) {
    Fake os;
    os.device.control_interface = role;
    UsbCdcStream stream(os);
    CHECK(!stream.begin_open(selection(), 0));
    CHECK(os.calls == std::vector<std::string>{"inspect"} && os.closes == 0);
  }
  for (unsigned variant = 0; variant < 3; ++variant) {
    Fake os;
    if (variant == 0)
      os.device.usb_serial = "foreign";
    if (variant == 1)
      ++os.device.vendor_id;
    if (variant == 2)
      ++os.device.product_id;
    UsbCdcStream stream(os);
    CHECK(!stream.begin_open(selection(), 0) && os.calls.size() == 1);
  }
  for (unsigned variant = 0; variant < 6; ++variant) {
    Fake os;
    auto s = selection();
    if (variant == 0)
      s.path = "relative";
    if (variant == 1)
      s.path += '\0';
    if (variant == 2)
      s.usb_serial.clear();
    if (variant == 3)
      s.usb_serial += '\n';
    if (variant == 4)
      s.vendor_id = 0;
    if (variant == 5)
      s.product_id = 0;
    UsbCdcStream stream(os);
    CHECK(!stream.begin_open(s, 0) && os.calls.empty());
  }
  for (const std::string operation : {"inspect", "open", "verify", "exclusive",
                                      "raw", "dtr-off", "discard"}) {
    Fake os;
    os.fail_on = operation;
    UsbCdcStream stream(os);
    CHECK(!stream.begin_open(selection(), 0));
    CHECK(stream.state() == CdcState::Failed && !stream.diagnostic().empty());
    CHECK(os.closes == (operation == "inspect" || operation == "open" ? 0 : 1));
    if (operation == "exclusive" || operation == "verify")
      CHECK(std::find(os.calls.begin(), os.calls.end(), "dtr-off") ==
            os.calls.end());
    stream.close();
    CHECK(os.closes <= 1);
  }
  Fake os;
  UsbCdcStream stream(os);
  CHECK(stream.begin_open(selection(), 10));
  CHECK(os.calls ==
        std::vector<std::string>({"inspect", "open", "verify", "exclusive",
                                  "raw", "dtr-off", "discard"}));
  CHECK((os.open_flags & (O_NONBLOCK | O_NOCTTY | O_CLOEXEC | O_NOFOLLOW)) ==
        (O_NONBLOCK | O_NOCTTY | O_CLOEXEC | O_NOFOLLOW));
  CHECK(!stream.begin_open(selection(), 20));
  std::array<std::uint8_t, 4> data{};
  CHECK(stream.read(data).state == IoState::Closed &&
        stream.write(data).state == IoState::Closed);
  stream.poll_open(800); // Setup time cannot count as DTR-low quiet time.
  stream.poll_open(899);
  CHECK(stream.state() == CdcState::Resetting);
  stream.poll_open(900);
  CHECK(stream.state() == CdcState::Ready);
  CHECK(os.calls.back() == "dtr-on");
  stream.close();
  stream.close();
  CHECK(os.closes == 1 && !stream.cleanup_failed());
  CHECK(os.calls.back() == "unexclusive");

  for (unsigned scenario = 0; scenario < 6; ++scenario) {
    Fake system;
    UsbCdcStream s(system);
    CHECK(s.begin_open(selection(), 10));
    s.poll_open(20);
    if (scenario == 0)
      s.poll_open(19);
    if (scenario == 1)
      s.poll_open(2010);
    if (scenario == 2) {
      system.device.usb_serial = "replacement";
      s.poll_open(120);
    }
    if (scenario == 3) {
      system.fail_on = "dtr-on";
      s.poll_open(120);
    }
    if (scenario == 4) {
      system.fail_on = "discard";
      s.poll_open(120);
    }
    if (scenario == 5)
      s.close();
    CHECK(s.state() != CdcState::Ready && system.closes == 1);
  }
  for (const std::string failure :
       {"dtr-off", "discard", "unexclusive", "close"}) {
    Fake system;
    UsbCdcStream s(system);
    open_ready(s);
    system.fail_on = failure;
    system.close_error = failure == "close" ? EINTR : 0;
    s.close();
    CHECK(s.cleanup_failed() && system.closes == 1);
    s.close();
    CHECK(system.closes == 1);
  }
  Fake zero;
  zero.next_fd = 0;
  {
    UsbCdcStream s(zero);
    open_ready(s);
  }
  CHECK(zero.closes == 1); // Descriptor zero is owned too.
}

void io_contract() {
  std::array<std::uint8_t, 5000> data{};
  for (bool writing : {false, true}) {
    for (int error : {EAGAIN, EINTR, EIO, ENODEV, EBADF}) {
      Fake os;
      UsbCdcStream stream(os);
      open_ready(stream);
      os.automatic_io = false;
      os.read_result = os.write_result = {-1, error};
      const auto result = writing ? stream.write(data) : stream.read(data);
      CHECK(result.count == 0);
      CHECK(result.state == (error == EAGAIN || error == EINTR
                                 ? IoState::WouldBlock
                                 : IoState::Failed));
      CHECK(os.closes == (result.state == IoState::Failed ? 1 : 0));
    }
    for (int events :
         {POLLHUP, POLLERR, POLLNVAL, POLLHUP | POLLIN | POLLOUT}) {
      Fake os;
      UsbCdcStream s(os);
      open_ready(s);
      os.poll_result.events = events;
      CHECK((writing ? s.write(data) : s.read(data)).state == IoState::Failed);
      CHECK(os.calls.end() == std::find(os.calls.begin(), os.calls.end(),
                                        writing ? "write" : "read"));
    }
    for (CdcPoll p :
         {CdcPoll{0, 0, 0}, {-1, 0, EINTR}, {-1, 0, EAGAIN}, {1, 0, 0}}) {
      Fake os;
      UsbCdcStream s(os);
      open_ready(s);
      os.poll_result = p;
      CHECK((writing ? s.write(data) : s.read(data)).state ==
            IoState::WouldBlock);
      CHECK(os.closes == 0);
    }
    Fake os;
    UsbCdcStream s(os);
    open_ready(s);
    os.automatic_io = false;
    os.read_result = os.write_result = {4097, 0};
    CHECK((writing ? s.write(data) : s.read(data)).state == IoState::Failed);
  }
  Fake os;
  UsbCdcStream s(os);
  open_ready(s);
  os.write_limit = 3;
  os.read_limit = 2;
  const std::array<std::uint8_t, 8> binary{0, 10, 13, 17, 19, 0x7f, 0x80, 0xff};
  std::size_t sent = 0;
  while (sent < binary.size()) {
    auto r = s.write(std::span(binary).subspan(sent));
    CHECK(r.state == IoState::Progress);
    sent += r.count;
  }
  CHECK(os.outgoing == std::vector<std::uint8_t>(binary.begin(), binary.end()));
  os.incoming.assign(binary.begin(), binary.end());
  std::vector<std::uint8_t> received;
  while (received.size() < binary.size()) {
    auto r = s.read(data);
    CHECK(r.state == IoState::Progress);
    received.insert(received.end(), data.begin(), data.begin() + r.count);
  }
  CHECK(received == os.outgoing);
  CHECK(s.read(data).state == IoState::WouldBlock);
  os.automatic_io = false;
  os.read_result = {0, 0};
  CHECK(s.read(data).state == IoState::WouldBlock);
  os.write_result = {0, 0};
  CHECK(s.write(data).state == IoState::Failed);
  CHECK(s.read(data).state == IoState::Closed);
  CHECK(s.write({}).state == IoState::Closed);
  for (const std::string op : {"poll", "read", "write"}) {
    Fake system;
    UsbCdcStream stream(system);
    open_ready(stream);
    system.fail_on = op;
    CHECK((op == "write" ? stream.write(data) : stream.read(data)).state ==
          IoState::Failed);
    CHECK(system.closes == 1);
  }
}

struct Pty {
  int master{-1};
  std::string slave;
  Pty() {
    master = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0)
      throw std::system_error(errno, std::generic_category(),
                              "create test PTY");
    CHECK(::grantpt(master) == 0 && ::unlockpt(master) == 0);
    CHECK(::fcntl(master, F_SETFL, O_NONBLOCK) == 0);
    CHECK(::fcntl(master, F_SETFD, FD_CLOEXEC) == 0);
    char *name = ::ptsname(master);
    CHECK(name);
    slave = name;
  }
  ~Pty() {
    if (master >= 0)
      ::close(master);
  }
};
struct PtySystem : PosixCdcSystem {
  std::string path;
  int descriptor{-1};
  std::vector<bool> modem;
  explicit PtySystem(std::string p) : path(std::move(p)) {}
  CdcIdentity inspect(const std::string &p) override {
    CHECK(p == path);
    struct stat info{};
    CHECK(::stat(p.c_str(), &info) == 0);
    auto i = identity();
    i.path = p;
    i.device_number = info.st_rdev;
    return i;
  }
  int open(const std::string &p, int flags) override {
    descriptor = PosixCdcSystem::open(p, flags);
    return descriptor;
  }
  void dtr(int, bool on) override {
    modem.push_back(on);
  } // PTYs have no USB modem lines.
  void raw(int fd) override {
    // Seed inherited processing/flow flags before running the actual setup.
    termios inherited{};
    CHECK(::tcgetattr(fd, &inherited) == 0);
    inherited.c_iflag |= IXOFF | IXON | ICRNL | ISTRIP;
#ifdef CDTR_IFLOW
    inherited.c_cflag |= CDTR_IFLOW | CDSR_OFLOW | CCAR_OFLOW;
#endif
    CHECK(::tcsetattr(fd, TCSANOW, &inherited) == 0);
    PosixCdcSystem::raw(fd);
    termios actual{};
    CHECK(::tcgetattr(fd, &actual) == 0);
#ifdef CDTR_IFLOW
    CHECK(!(actual.c_cflag & (CDTR_IFLOW | CDSR_OFLOW | CCAR_OFLOW)));
#endif
  }
};
void pseudo_terminal() {
  Pty p;
  PtySystem os(p.slave);
  UsbCdcStream stream(os);
  auto selected = selection();
  selected.path = p.slave;
  if (!stream.begin_open(selected, 0))
    throw std::runtime_error("PTY open: " + stream.diagnostic());
  stream.poll_open(0);
  stream.poll_open(100);
  CHECK(stream.state() == CdcState::Ready);
  CHECK(os.modem == std::vector<bool>({false, true}));
  PtySystem contender(p.slave);
  UsbCdcStream other(contender);
  CHECK(!other.begin_open(selected, 100));
  CHECK(contender.modem
            .empty()); // Failed ownership must not drop the owner's DTR.
  CHECK(stream.state() == CdcState::Ready);
  CHECK((::fcntl(os.descriptor, F_GETFL) & O_NONBLOCK) != 0);
  CHECK((::fcntl(os.descriptor, F_GETFD) & FD_CLOEXEC) != 0);
  // Every byte value, including tty control characters, survives both
  // directions.
  std::array<std::uint8_t, 256> payload{}, buffer{};
  for (std::size_t i = 0; i < payload.size(); ++i)
    payload[i] = static_cast<std::uint8_t>(i);
  CHECK(::write(p.master, payload.data(), payload.size()) == 256);
  std::size_t count{};
  for (unsigned tries = 0; tries < 10000 && count < 256; ++tries) {
    auto r = stream.read(std::span(buffer).subspan(count));
    CHECK(r.state == IoState::Progress || r.state == IoState::WouldBlock);
    count += r.count;
  }
  CHECK(count == 256 && payload == buffer);
  count = 0;
  for (unsigned tries = 0; tries < 10000 && count < 256; ++tries) {
    auto r = stream.write(std::span(payload).subspan(count));
    CHECK(r.state == IoState::Progress || r.state == IoState::WouldBlock);
    count += r.count;
  }
  CHECK(count == 256);
  count = 0;
  for (unsigned tries = 0; tries < 10000 && count < 256; ++tries) {
    auto n = ::read(p.master, buffer.data() + count, 256 - count);
    CHECK(n >= 0 || errno == EAGAIN || errno == EINTR);
    if (n > 0)
      count += static_cast<std::size_t>(n);
  }
  CHECK(count == 256 && payload == buffer);
  CHECK(stream.read(buffer).state == IoState::WouldBlock);
  // Backpressure returns promptly without draining or spinning inside the
  // adapter.
  bool blocked = false;
  for (unsigned tries = 0; tries < 10000; ++tries) {
    auto r = stream.write(payload);
    CHECK(r.state == IoState::Progress || r.state == IoState::WouldBlock);
    if (r.state == IoState::WouldBlock) {
      blocked = true;
      break;
    }
  }
  CHECK(blocked);
  stream.close();
  CHECK(!stream.cleanup_failed());
  CHECK(::fcntl(os.descriptor, F_GETFD) < 0 && errno == EBADF);
  CHECK(stream.begin_open(selected, 200));
  stream.poll_open(200);
  stream.poll_open(300);
  CHECK(stream.state() ==
        CdcState::Ready); // TIOCEXCL was released, no manual reset.
  ::close(p.master);
  p.master = -1;
  CHECK(stream.read(buffer).state == IoState::Failed);
}

struct Tree {
  std::filesystem::path root, usb, interface;
  explicit Tree(const std::string &tty) {
    char path[] = "/tmp/wsprrypi-cdc-XXXXXX";
    char *created = ::mkdtemp(path);
    CHECK(created);
    root = created;
    usb = root / "devices/usb/board";
    interface = usb / "board:1.2";
    std::filesystem::create_directories(interface / "tty/ttyACM9");
    std::filesystem::create_directories(root / "dev/char");
    std::filesystem::create_directories(root / "drivers/cdc_acm");
    struct stat info{};
    CHECK(::stat(tty.c_str(), &info) == 0);
    std::filesystem::create_symlink(interface / "tty/ttyACM9",
                                    root / "dev/char" /
                                        (std::to_string(major(info.st_rdev)) +
                                         ":" +
                                         std::to_string(minor(info.st_rdev))));
    std::filesystem::create_symlink(root / "drivers/cdc_acm",
                                    interface / "driver");
    write(usb / "idVendor", "cafe\n");
    write(usb / "idProduct", "4012\n");
    write(usb / "serial", "PICO123\n");
    write(interface / "bInterfaceNumber", "02\n");
    write(interface / "bInterfaceClass", "02\n");
    write(interface / "bInterfaceSubClass", "02\n");
    write(interface / "bInterfaceProtocol", "00\n");
  }
  static void write(const std::filesystem::path &path, const std::string &s) {
    std::ofstream out(path, std::ios::binary);
    out << s;
    CHECK(out.good());
  }
  ~Tree() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};
void pairing() {
  Pty wtp_port, management_port, other_port;
  Tree tree(wtp_port.slave);
  const auto devices=tree.root/"device-nodes";
  std::filesystem::create_directories(tree.root/"class/tty");
  std::filesystem::create_directories(devices);
  const auto add=[&](Pty &port,const std::filesystem::path &usb,const std::string &name) {
    const auto interface=usb/"interface4";
    std::filesystem::create_directories(interface/"tty"/name);
    Tree::write(usb/"serial","PICO123\n");Tree::write(usb/"idVendor","cafe\n");Tree::write(usb/"idProduct","4012\n");
    Tree::write(interface/"bInterfaceNumber","04\n");Tree::write(interface/"bInterfaceClass","02\n");
    Tree::write(interface/"bInterfaceSubClass","02\n");Tree::write(interface/"bInterfaceProtocol","00\n");
    std::filesystem::create_symlink(tree.root/"drivers/cdc_acm",interface/"driver");
    struct stat info{};CHECK(::stat(port.slave.c_str(),&info)==0);
    std::filesystem::create_symlink(interface/"tty"/name,tree.root/"dev/char"/(std::to_string(major(info.st_rdev))+":"+std::to_string(minor(info.st_rdev))));
    std::filesystem::create_directory(tree.root/"class/tty"/name);
    std::filesystem::create_symlink(port.slave,devices/name);
  };
  add(management_port,tree.usb,"arbitrary-role-name");
  add(other_port,tree.root/"devices/usb/other-board","not-this-board");
  auto selected=selection();selected.path=wtp_port.slave;
  const auto paired=paired_linux_cdc(selected,PicoCdcRole::Management,tree.root,devices);
  CHECK(paired.path==management_port.slave && paired.role==PicoCdcRole::Management);
  // Duplicate serials on other physical parents do not select another board.
  CHECK(paired.path!=other_port.slave);
  std::filesystem::create_directory(tree.root/"class/tty"/"duplicate-role-alias");
  std::filesystem::create_symlink(management_port.slave,devices/"duplicate-role-alias");
  bool refused=false;try {(void)paired_linux_cdc(selected,PicoCdcRole::Management,tree.root,devices);}catch(...){refused=true;}
  CHECK(refused);
  Fake system;system.device.control_interface=4;UsbCdcStream stream(system);
  auto role=selection();role.role=PicoCdcRole::Management;
  CHECK(stream.begin_open(role,0));stream.close();
  CHECK(!stream.begin_open(selection(),0));
}
void metadata() {
  Pty p;
  Tree t(p.slave);
  auto i = inspect_linux_cdc(p.slave, t.root);
  CHECK(i.vendor_id == 0xcafe && i.product_id == 0x4012 &&
        i.usb_serial == "PICO123" && i.control_interface == 2);
  std::filesystem::create_symlink(p.slave, t.root / "alias");
  CHECK(inspect_linux_cdc((t.root / "alias").string(), t.root) == i);
  for (const auto &bad :
       {std::string(""), std::string("0x02\n"), std::string("002\n"),
        std::string("gg\n"), std::string(300, '2')}) {
    Tree::write(t.interface / "bInterfaceNumber", bad);
    bool failed = false;
    try {
      inspect_linux_cdc(p.slave, t.root);
    } catch (const std::exception &) {
      failed = true;
    }
    CHECK(failed);
  }
  Tree::write(t.interface / "bInterfaceNumber", "00\n");
  CHECK(inspect_linux_cdc(p.slave, t.root).control_interface ==
        0); // Resolver observes; selection rejects Console.
  Tree::write(t.interface / "bInterfaceNumber", "02\n");
  Tree::write(t.interface / "bInterfaceClass", "ff\n");
  bool failed = false;
  try {
    inspect_linux_cdc(p.slave, t.root);
  } catch (const std::exception &) {
    failed = true;
  }
  CHECK(failed);
  Tree::write(t.interface / "bInterfaceClass", "02\n");
  Tree::write(t.interface / "bInterfaceProtocol", "01\n");
  failed = false;
  try {
    inspect_linux_cdc(p.slave, t.root);
  } catch (const std::exception &) {
    failed = true;
  }
  CHECK(failed); // TinyUSB's pinned TUD_CDC_DESCRIPTOR uses protocol NONE (00).
  Tree::write(t.interface / "bInterfaceProtocol", "00\n");
  std::filesystem::remove(t.interface / "driver");
  std::filesystem::create_directories(t.root / "drivers/other");
  std::filesystem::create_symlink(t.root / "drivers/other",
                                  t.interface / "driver");
  failed = false;
  try {
    inspect_linux_cdc(p.slave, t.root);
  } catch (const std::exception &) {
    failed = true;
  }
  CHECK(failed);
  std::filesystem::remove(t.interface / "driver");
  std::filesystem::create_symlink(t.root / "drivers/cdc_acm",
                                  t.interface / "driver");
  std::filesystem::remove(t.usb / "serial");
  failed = false;
  try {
    inspect_linux_cdc(p.slave, t.root);
  } catch (const std::exception &) {
    failed = true;
  }
  CHECK(failed);
  Tree::write(t.root / "file", "not a device");
  failed = false;
  try {
    inspect_linux_cdc((t.root / "file").string(), t.root);
  } catch (const std::exception &) {
    failed = true;
  }
  CHECK(failed);
#ifndef __linux__
  PosixCdcSystem native;
  UsbCdcStream stream(native);
  auto s = selection();
  s.path = p.slave;
  CHECK(!stream.begin_open(s, 0));
  CHECK(stream.diagnostic().find("only on Linux") != std::string::npos);
#endif
}

const std::string sid(32, '1'), owner(32, '2'), device_id(32, '4'),
    boot(32, '5');
const std::string capabilities =
    R"({"profiles":["rf-events/1"],"modes":["tone"],"engine":"fake","frequency_ranges":[{"minimum_nhz":"1","maximum_nhz":"30000000000000000"}],"max_payload_bytes":65536,"max_events":162,"max_job_duration_ns":"110592000000","minimum_arm_lead_ns":"1000000","maximum_arm_ahead_ns":"10000000000","maximum_arm_uncertainty_ns":"1000000","maximum_holdover_age_ns":"1000000000","output_disable_timeout_ns":"1000000","minimum_lease_ms":5000,"maximum_lease_ms":60000,"response_cache_entries":8,"response_cache_ttl_seconds":300,"terminal_record_entries":8,"terminal_record_ttl_seconds":3600})";
struct ServerFake : Fake {
  wtp::FrameParser parser;
  std::vector<wtp::Operation> operations;
  bool claimed{};
  void discard(int fd) override {
    Fake::discard(fd);
    parser = wtp::FrameParser{};
  }
  CdcTransfer write(int fd, std::span<const std::uint8_t> bytes) override {
    auto result = Fake::write(fd, bytes);
    if (result.count <= 0)
      return result;
    auto frames =
        parser.feed(bytes.first(static_cast<std::size_t>(result.count)), 0);
    for (const auto &frame : frames.events)
      if (frame.kind == wtp::FrameEventKind::Payload) {
        auto decoded =
            wtp::decode({reinterpret_cast<const char *>(frame.payload.data()),
                         frame.payload.size()});
        CHECK(decoded);
        const auto &r = std::get<wtp::Request>(*decoded.message);
        operations.push_back(r.op);
        std::string body;
        if (r.op == wtp::Operation::Hello)
          body = "{\"selected_version\":\"WTP/1\",\"device_id\":\"" +
                 device_id + "\",\"boot_id\":\"" + boot +
                 "\",\"product\":\"fake\",\"firmware_version\":\"1\"}";
        else if (r.op == wtp::Operation::Status)
          body =
              "{\"boot_id\":\"" + boot +
              "\",\"state\":\"empty\",\"output_active\":false,\"owner_id\":" +
              (claimed ? "\"" + owner + "\"" : "null") +
              ",\"job_id\":null,\"terminal_records\":[]}";
        else if (r.op == wtp::Operation::Caps)
          body = capabilities;
        else if (r.op == wtp::Operation::Claim) {
          claimed = true;
          body = "{\"owner_id\":\"" + owner +
                 "\",\"granted_lease_ms\":5000,\"expires_monotonic_ns\":"
                 "\"5000000000\"}";
        } else
          throw std::runtime_error("unexpected test mutation");
        const std::string reply =
            "{\"type\":\"response\",\"protocol\":\"WTP/1\",\"session_id\":\"" +
            r.session_id + "\",\"request_id\":\"" + r.request_id +
            "\",\"op\":\"" + std::string(wtp::operation_name(r.op)) +
            "\",\"ok\":true,\"body\":" + body + "}";
        auto checked_reply = wtp::decode(reply);
        if (!checked_reply)
          throw std::runtime_error("Test reply: " + checked_reply.error);
        auto encoded = wtp::encode_frame(
            {reinterpret_cast<const std::uint8_t *>(reply.data()),
             reply.size()});
        incoming.insert(incoming.end(), encoded.begin(), encoded.end());
      }
    return result;
  }
};
void session_integration() {
  ServerFake os;
  os.write_limit = 3;
  os.read_limit = 7;
  UsbCdcStream stream(os);
  open_ready(stream);
  wtp::Session session({sid, owner, device_id});
  CHECK(session.connect(stream, 100));
  CHECK(!session.request(wtp::Operation::Claim, wtp::LeaseRequest{owner, 5000},
                         100));
  auto pump = [&](std::uint64_t now) {
    for (unsigned i = 0; i < 10000; ++i) {
      session.poll(now);
      if (session.phase() == wtp::SessionPhase::Ready && !session.busy() &&
          !session.needs_status())
        return;
      if (session.phase() == wtp::SessionPhase::Fault ||
          session.phase() == wtp::SessionPhase::Disconnected)
        throw std::runtime_error("USB session: " + session.diagnostic() + "; " +
                                 stream.diagnostic());
    }
    throw std::runtime_error("fake session stalled");
  };
  pump(100);
  CHECK(os.operations == std::vector<wtp::Operation>({wtp::Operation::Hello,
                                                      wtp::Operation::Status,
                                                      wtp::Operation::Caps}));
  CHECK(session.request(wtp::Operation::Claim, wtp::LeaseRequest{owner, 5000},
                        100));
  pump(100);
  CHECK(session.take_result()->kind == wtp::ResultKind::Acknowledged);
  wtp::Job job{std::string(32, '3'),
               wtp::Mode::Tone,
               1'000'000,
               {{0, 1'000'000, true, 1'000'000'000}},
               false};
  CHECK(session.request(wtp::Operation::Load, job, 100));
  session.poll(100); // Three bytes accepted, never a complete LOAD.
  stream.close();
  session.poll(101);
  CHECK(session.take_result()->kind == wtp::ResultKind::Unknown &&
        session.uncertain());
  const auto previous = os.operations.size();
  open_ready(stream, 200);
  CHECK(session.connect(stream, 300));
  pump(300);
  CHECK(os.operations.size() == previous + 3);
  CHECK(os.operations[previous] == wtp::Operation::Hello &&
        os.operations[previous + 1] == wtp::Operation::Status);
  CHECK(session.uncertain() &&
        !session.request(wtp::Operation::Load, job, 300));
  CHECK(!session.retry_uncertain(
      300)); // Empty STATUS does not prove retransmission safe.
  CHECK(session.request(wtp::Operation::Status, wtp::Empty{}, 300));
  os.poll_result = {0, 0, 0};
  session.poll(301);
  session.poll(5299);
  CHECK(session.busy());
  session.poll(5300); // No accepted write bytes: idle deadline closes locally.
  CHECK(session.take_result()->kind == wtp::ResultKind::NotSent);
  CHECK(stream.state() == CdcState::Closed && session.uncertain());
  os.poll_result = {1, POLLIN | POLLOUT, 0};
  open_ready(stream, 6000);
  CHECK(session.connect(stream, 6100));
  pump(6100);
  CHECK(session.request(wtp::Operation::Status, wtp::Empty{}, 6100));
  os.write_limit = 4096;
  os.read_limit = 0;
  session.poll(6100);
  session.poll(11099);
  CHECK(session.busy());
  session.poll(
      11100); // Submitted request, stalled reply: unknown, never success.
  CHECK(session.take_result()->kind == wtp::ResultKind::Unknown);
  CHECK(stream.state() == CdcState::Closed && session.uncertain());
  session.disconnect();
}
} // namespace
int main() {
  try {
    setup_and_identity();
    pairing();
    io_contract();
    metadata();
    pseudo_terminal();
    session_integration();
    std::cout << "WTP USB CDC checks passed: " << checks
              << " (fakes, synthetic sysfs and PTYs only)\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
