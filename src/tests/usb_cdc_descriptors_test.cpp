// SPDX-License-Identifier: MIT
#include "wtp_integration/usb_cdc_descriptors.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>
using Bytes=std::vector<std::uint8_t>;
Bytes descriptors(unsigned control=4,unsigned data=5) {
    Bytes result{9,2,0,0,2,1,0,0x80,50,
        9,4,std::uint8_t(control),0,1,2,2,0,3,
        5,0x24,6,std::uint8_t(control),std::uint8_t(data),
        9,4,std::uint8_t(data),0,2,10,0,0,0};
    result[2]=std::uint8_t(result.size());return result;
}
template<class F> void refused(F f){bool failed=false;try{f();}catch(const std::exception &){failed=true;}assert(failed);}
int main(){
    for(const auto control:{0U,2U,4U,6U,11U}) {
        // Deliberately nonadjacent data interfaces prove there is no +1 rule.
        const auto data=control+19;const auto bytes=descriptors(control,data);
        const auto expected=wsprrypi::CdcDescriptorBinding{control,data};
        assert(wsprrypi::cdc_descriptor_binding(bytes,control)==expected);
        assert(wsprrypi::cdc_descriptor_binding(bytes,data)==expected);
    }
    const auto valid=descriptors();
    for(std::size_t length=0;length<valid.size();++length)
        refused([&]{wsprrypi::cdc_descriptor_binding(std::span(valid).first(length),4);});
    refused([&]{wsprrypi::cdc_descriptor_binding(valid,0);});
    for(const auto offset:{0U,1U,2U,4U,9U,10U,12U,14U,15U,16U,18U,19U,20U,21U,22U,23U,24U,25U,28U,29U,30U}) {
        auto bad=valid;bad[offset]=255;
        refused([&]{wsprrypi::cdc_descriptor_binding(bad,4);});
    }
    auto missing=valid;missing.erase(missing.begin()+18,missing.begin()+23);missing[2]=std::uint8_t(missing.size());
    refused([&]{wsprrypi::cdc_descriptor_binding(missing,4);});
    auto duplicated=valid;duplicated.insert(duplicated.begin()+23,valid.begin()+18,valid.begin()+23);duplicated[2]=std::uint8_t(duplicated.size());
    refused([&]{wsprrypi::cdc_descriptor_binding(duplicated,4);});
    std::cout<<"PASS CDC Union-based role binding, nonadjacent interfaces, truncation, malformed classes/lengths, missing/duplicate unions, and unknown roles\n";
}
