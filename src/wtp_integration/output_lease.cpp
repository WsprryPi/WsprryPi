// SPDX-License-Identifier: MIT
#include "output_lease.hpp"
#include <map>
#include <mutex>
#include <stdexcept>
namespace wsprrypi {
namespace { std::mutex mutex; std::map<std::string,std::weak_ptr<void>> owners; }
std::shared_ptr<void> wtp_reserve_output_identity(const std::string& id) {
    if (id.size()!=32) throw std::runtime_error("A complete expected WTP identity is required");
    std::lock_guard lock(mutex);
    for(auto it=owners.begin();it!=owners.end();) it=it->second.expired()?owners.erase(it):std::next(it);
    if (owners.contains(id)) throw std::runtime_error("This WTP output is already selected by another local control path");
    auto owner=std::make_shared<int>(0); owners.emplace(id,owner); return owner;
}
bool wtp_output_identity_in_use(const std::string& id) {
    std::lock_guard lock(mutex); auto it=owners.find(id); return it!=owners.end()&&!it->second.expired();
}
}
