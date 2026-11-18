// SPDX-License-Identifier: MIT
#include "wtp_integration/pi_pair_tls.hpp"
#include "pairing_provider/retirement-vector.hpp"
#include <iostream>
using namespace wsprrypi;
namespace {
unsigned checks{};
void check(bool value){++checks;if(!value)throw std::runtime_error("retirement key check "+std::to_string(checks));}
template<class F>void refuses(F action){bool held=false;try{action();}catch(const PiPairCryptoError &e){held=e.code()==PiPairCryptoFailure::InvalidShape;}check(held);}
std::string id(char c){return std::string(32,c);}
}
int main(){try{
    const auto verifier=PiPairSecret::from_storage(retirement_fixture::verifier);
    const auto derived=pi_pair_retirement_tls_key(verifier,id('a'),id('b'),id('c'),"7");
    check(derived.storage_encoding()==retirement_fixture::transport_psk);
    check(derived.storage_encoding()!=verifier.storage_encoding());
    for(const auto *generation:{"","0","00","07","+7","-7","7 ","7x","18446744073709551616"})
        refuses([&]{(void)pi_pair_retirement_tls_key(verifier,id('a'),id('b'),id('c'),generation);});
    for(const auto &bad:{std::string(),std::string(31,'a'),id('A'),std::string(33,'a'),std::string(31,'a')+"/"}){
        refuses([&]{(void)pi_pair_retirement_tls_key(verifier,bad,id('b'),id('c'),"7");});
        refuses([&]{(void)pi_pair_retirement_tls_key(verifier,id('a'),bad,id('c'),"7");});
        refuses([&]{(void)pi_pair_retirement_tls_key(verifier,id('a'),id('b'),bad,"7");});
    }
    refuses([&]{(void)pi_pair_retirement_tls_key(verifier,id('a'),id('a'),id('c'),"7");});
    for(unsigned field=0;field<5;++field){
        const auto changed=pi_pair_retirement_tls_key(verifier,field==0?id('d'):id('a'),field==1?id('d'):id('b'),
            field==2?id('d'):id('c'),field==3?"8":field==4?"18446744073709551615":"7");
        check(changed.storage_encoding()!=derived.storage_encoding());
    }
    check(pi_pair_retirement_tls_key(verifier,id('b'),id('a'),id('c'),"7").storage_encoding()!=derived.storage_encoding());
    std::array<unsigned char,32> other{};
    check(pi_pair_retirement_tls_key(PiPairSecret(other),id('a'),id('b'),id('c'),"7").storage_encoding()!=derived.storage_encoding());
    // No caller chooses/reverses the purpose or role strings. The same explicit
    // target/controller order is used at both ends of the TLS callback.
    check(pi_pair_retirement_tls_key(verifier,id('a'),id('b'),id('c'),"7").storage_encoding()==derived.storage_encoding());
    std::cout<<"PASS fixed-purpose OpenSSL retirement TLS key derivation: "<<checks<<" checks\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
