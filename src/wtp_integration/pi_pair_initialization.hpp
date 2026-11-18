// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <utility>
namespace wsprrypi {
// Positive pre-first-use event, not an inference from absent pairing files.
// Only the existing trusted installation/retirement owners can issue this.
// There is no browser/JSON/parser factory and the capability cannot be copied.
class PiPairInitializationProof {
public:
    enum class Source {TransmitterInstallation,ControllerEstablishment};
    PiPairInitializationProof(const PiPairInitializationProof &)=delete;
    PiPairInitializationProof &operator=(const PiPairInitializationProof &)=delete;
    PiPairInitializationProof(PiPairInitializationProof &&other) noexcept
        :self_(std::move(other.self_)),installation_(std::move(other.installation_)),source_(other.source_){
        other.self_.clear();other.installation_.clear();
    }
    PiPairInitializationProof &operator=(PiPairInitializationProof &&)=delete;
    bool valid() const noexcept{return !self_.empty()&&!installation_.empty();}
    const std::string &device_id() const noexcept{return self_;}
    const std::string &installation_id() const noexcept{return installation_;}
    Source source() const noexcept{return source_;}
private:
    friend class PiManagedStore;
    friend class ControllerRetirementBook;
    PiPairInitializationProof(std::string self,std::string installation,Source source)
        :self_(std::move(self)),installation_(std::move(installation)),source_(source){}
    std::string self_,installation_;
    Source source_;
};
} // namespace wsprrypi
