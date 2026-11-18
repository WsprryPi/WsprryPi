// SPDX-License-Identifier: MIT
#pragma once
#if defined(__linux__) && defined(WSPRRYPI_HAVE_AVAHI)
#include <avahi-common/strlst.h>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>
namespace wsprrypi {
// Avahi's parsed list is reverse wire order. Preserve the wire's first-field
// check without modifying or taking ownership of the callback's native list.
inline std::vector<std::string> pi_pair_discovery_avahi_fields(AvahiStringList *txt){
    std::vector<std::string> fields;
    for(auto *item=txt;item;item=avahi_string_list_get_next(item)){
        const auto length=avahi_string_list_get_size(item);const auto *bytes=avahi_string_list_get_text(item);
        if(!bytes||length>255||fields.size()>=7)throw std::invalid_argument("invalid_pairing_discovery");
        fields.emplace_back(reinterpret_cast<const char *>(bytes),length);
    }
    std::reverse(fields.begin(),fields.end());return fields;
}
}
#endif
