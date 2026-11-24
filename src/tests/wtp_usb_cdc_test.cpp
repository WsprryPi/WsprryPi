// SPDX-License-Identifier: MIT
#include "wtp_integration/usb_cdc.hpp"
#include "wtp_integration/usb_discovery.hpp"
#include "wtp_settings_json.hpp"

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
 for (unsigned role_number : {4U, 6U}) {
  const auto selected_role = static_cast<PicoCdcRole>(role_number);
  Pty wtp_port, management_port, other_port;
  Tree tree(wtp_port.slave);
  const auto devices=tree.root/"device-nodes";
  std::filesystem::create_directories(tree.root/"class/tty");
  std::filesystem::create_directories(devices);
  const auto add=[&](Pty &port,const std::filesystem::path &usb,const std::string &name) {
    const auto interface=usb/("interface" + std::to_string(role_number));
    std::filesystem::create_directories(interface/"tty"/name);
    Tree::write(usb/"serial","PICO123\n");Tree::write(usb/"idVendor","cafe\n");Tree::write(usb/"idProduct","4012\n");
    Tree::write(interface/"bInterfaceNumber",role_number == 6 ? "06\n" : "04\n");Tree::write(interface/"bInterfaceClass","02\n");
    Tree::write(interface/"bInterfaceSubClass","02\n");Tree::write(interface/"bInterfaceProtocol","00\n");
    std::filesystem::create_symlink(tree.root/"drivers/cdc_acm",interface/"driver");
    struct stat info{};CHECK(::stat(port.slave.c_str(),&info)==0);
    std::filesystem::create_symlink(interface/"tty"/name,tree.root/"dev/char"/(std::to_string(major(info.st_rdev))+":"+std::to_string(minor(info.st_rdev))));
    std::filesystem::create_directory(tree.root/"class/tty"/name);
    std::filesystem::create_symlink(port.slave,devices/name);
  };
  auto missing=selection();missing.path=wtp_port.slave;
  bool unsupported=false;
  try { (void)paired_linux_cdc(missing,selected_role,tree.root,devices); }
  catch(const std::exception &e) {
    unsupported = role_number == 6 ? std::string(e.what()) == "dedicated_logging_unsupported" : true;
  }
  CHECK(unsupported); // Metadata-only: never open or fall back to Console.
  add(management_port,tree.usb,"arbitrary-role-name");
  add(other_port,tree.root/"devices/usb/other-board","not-this-board");
  auto selected=selection();selected.path=wtp_port.slave;
  const auto paired=paired_linux_cdc(selected,selected_role,tree.root,devices);
  CHECK(paired.path==management_port.slave && paired.role==selected_role);
  // Duplicate serials on other physical parents do not select another board.
  CHECK(paired.path!=other_port.slave);
  if (selected_role == PicoCdcRole::Management) {
    const auto from_legacy = management_linux_cdc(selected,tree.root,devices);
    CHECK(from_legacy.path == management_port.slave);
    auto direct = selected;
    direct.path = management_port.slave;
    const auto resolved = management_linux_cdc(direct,tree.root,devices);
    CHECK(resolved.path == management_port.slave && resolved.role == PicoCdcRole::Management);
    CHECK(wtp_linux_cdc(selected,tree.root,devices).path == wtp_port.slave);
    const auto wtp_name = tree.root / "class/tty/legacy-execution";
    std::filesystem::create_directory(wtp_name);
    std::filesystem::create_symlink(wtp_port.slave,devices / "legacy-execution");
    const auto execution = wtp_linux_cdc(direct,tree.root,devices);
    CHECK(execution.path == wtp_port.slave && execution.role == PicoCdcRole::Wtp);
    auto wrong = direct;
    wrong.usb_serial = "OTHER";
    bool mismatch = false;
    try { (void)management_linux_cdc(wrong,tree.root,devices); } catch (...) { mismatch = true; }
    CHECK(mismatch);
    const auto management_interface = std::filesystem::path(inspect_linux_cdc(direct.path,tree.root).interface_path);
    Tree::write(management_interface / "bInterfaceNumber", "00\n");
    bool console = false;
    try { (void)management_linux_cdc(direct,tree.root,devices); } catch (...) { console = true; }
    CHECK(console);
    Tree::write(management_interface / "bInterfaceNumber", "04\n");
  }
  std::filesystem::create_directory(tree.root/"class/tty"/"duplicate-role-alias");
  std::filesystem::create_symlink(management_port.slave,devices/"duplicate-role-alias");
  bool refused=false;try {(void)paired_linux_cdc(selected,selected_role,tree.root,devices);}catch(...){refused=true;}
  CHECK(refused);
  if (selected_role == PicoCdcRole::Management) {
    auto direct = selected;
    direct.path = management_port.slave;
    bool ambiguous = false;
    try { (void)management_linux_cdc(direct,tree.root,devices); } catch (...) { ambiguous = true; }
    CHECK(ambiguous);
    std::filesystem::remove(devices / "duplicate-role-alias");
    std::filesystem::remove(tree.root / "class/tty/duplicate-role-alias");
    // A management-only device remains usable without any WTP sibling.
    std::filesystem::remove_all(tree.interface);
    CHECK(management_linux_cdc(direct,tree.root,devices).path == management_port.slave);
    bool no_execution = false;
    try { (void)wtp_linux_cdc(direct,tree.root,devices); } catch (...) { no_execution = true; }
    CHECK(no_execution);
  }
  Fake system;system.device.control_interface=role_number;UsbCdcStream stream(system);
  auto role=selection();role.role=selected_role;
  CHECK(stream.begin_open(role,0));stream.close();
  CHECK(!stream.begin_open(selection(),0));
  if (selected_role == PicoCdcRole::Logging) {
    std::filesystem::remove(devices / "duplicate-role-alias");
    std::filesystem::remove(tree.root / "class/tty/duplicate-role-alias");
    CHECK(logging_linux_cdc(selected,tree.root,devices).path == management_port.slave);
    Tree::write(tree.interface / "bInterfaceNumber", "04\n");
    CHECK(logging_linux_cdc(selected,tree.root,devices).path == management_port.slave);
  }
 }
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
  CHECK(stream.state()==CdcState::Failed&&!stream.identity()&&!stream.diagnostic().empty());
#endif
}

void discovery(bool management = false) {
  Pty port;
  Tree tree(port.slave);
  const auto devices = tree.root / "nodes";
  std::filesystem::create_directories(tree.root / "class/tty/arbitrary-name");
  std::filesystem::create_directories(devices / "serial/by-id");
  std::filesystem::create_symlink(port.slave, devices / "arbitrary-name");
  Tree::write(tree.usb / "product", "WsprryPico\n");
  Tree::write(tree.interface / "interface", management ? "WsprryPico Management\n" : "WsprryPico WTP\n");
  const auto role = management ? "04\n" : "02\n";
  Tree::write(tree.interface / "bInterfaceNumber", role);
  const auto discover = [&](const auto &root, const auto &dev) {
    return management ? pico_management_usb_discovery(root,dev) : wtp_usb_discovery(root,dev);
  };
  // Discovery reads synthetic metadata only. The PTY stays unopened by it.
  const char *disabled = std::getenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS");
  const std::optional<std::string> previous = disabled ? std::optional<std::string>(disabled) : std::nullopt;
  ::unsetenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS");
  const auto snapshot = discover(tree.root, devices);
  CHECK(snapshot.at("available") && snapshot.at("candidates").size() == 1);
  const auto candidate = snapshot.at("candidates").at(0);
  const auto id = candidate.at("id").get<std::string>();
  auto settings = parse_wtp_settings(candidate.at("settings"), true);
  CHECK(settings.path == port.slave && settings.usb_serial == "PICO123");
  CHECK(settings.vendor_id == 0xcafe && settings.product_id == 0x4012 && settings.device_id.empty());
  validate_discovered_usb(snapshot, id, settings);
  ++settings.vendor_id;
  bool refused = false;
  try { validate_discovered_usb(snapshot, id, settings); } catch (...) { refused = true; }
  CHECK(refused);
  const auto stable = devices / "serial/by-id/pico-role2";
  std::filesystem::create_symlink(port.slave, stable);
  CHECK(discover(tree.root, devices).at("candidates").at(0).at("settings").at("Endpoint") == stable.string());
  // Other roles never become candidates for the requested function.
  for (const auto &other : {"00\n", management ? "02\n" : "04\n", "06\n"}) {
    Tree::write(tree.interface / "bInterfaceNumber", other);
    CHECK(discover(tree.root, devices).at("candidates").empty());
  }
  Tree::write(tree.interface / "bInterfaceNumber", role);
  Tree::write(tree.usb / "product", "Other serial device\n");
  CHECK(discover(tree.root, devices).at("candidates").empty());
  Tree::write(tree.usb / "product", "WsprryPico\n");
  std::filesystem::create_directories(tree.root / "class/tty/duplicate");
  std::filesystem::create_symlink(port.slave, devices / "duplicate");
  CHECK(discover(tree.root, devices).at("candidates").empty());
  std::filesystem::remove(devices / "duplicate");
  std::filesystem::remove(devices / "arbitrary-name");
  const auto unplugged = discover(tree.root, devices);
  CHECK(unplugged.at("candidates").empty());
  refused = false;
  try { validate_discovered_usb(unplugged, id, settings); } catch (...) { refused = true; }
  CHECK(refused);
  ::setenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS", "1", 1);
  CHECK(!discover(tree.root, devices).at("available").get<bool>());
  if (previous) ::setenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS", previous->c_str(), 1);
  else ::unsetenv("WSPRRYPI_DISABLE_HARDWARE_ACCESS");
}

} // namespace
int main() {
  try {
    setup_and_identity();
    pairing();
    io_contract();
    metadata();
    discovery();
    discovery(true);
    pseudo_terminal();
    std::cout << "Management USB CDC checks passed: " << checks
              << " (fakes, synthetic sysfs and PTYs only)\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
