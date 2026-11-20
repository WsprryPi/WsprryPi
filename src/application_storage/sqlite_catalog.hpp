// SPDX-License-Identifier: MIT
#pragma once
#include "sqlite_state_store.hpp"
#include "wtp_integration/catalog.hpp"

namespace wsprrypi {
// Both the catalog and managed settings coordinator must share this one store.
// The adapter grants no enrollment/management authority and initializes nothing.
CatalogStorage sqlite_catalog_storage(std::shared_ptr<SqliteStateStore>);
} // namespace wsprrypi
