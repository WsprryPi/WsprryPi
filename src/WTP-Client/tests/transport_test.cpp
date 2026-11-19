// SPDX-License-Identifier: MIT
#include "wtp/transport.hpp"
#include <type_traits>
using namespace wsprrypi::wtp;
static_assert(std::is_abstract_v<ByteStream>);
static_assert(std::has_virtual_destructor_v<ByteStream>);
struct ClosedStream final:ByteStream {
    bool &destroyed;
    explicit ClosedStream(bool &flag):destroyed(flag){}
    ~ClosedStream() override{destroyed=true;}
    IoResult read(std::span<std::uint8_t>) override{return {IoState::Closed};}
    IoResult write(std::span<const std::uint8_t>) override{return {IoState::Closed};}
    void close() noexcept override{}
};
int main(){bool destroyed=false;ByteStream *stream=new ClosedStream(destroyed);
    const auto result=stream->write({});delete stream;
    return !destroyed||result.state!=IoState::Closed||result.count!=0;}
