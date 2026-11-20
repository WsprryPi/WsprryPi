// SPDX-License-Identifier: MIT
#include "sqlite_catalog.hpp"

namespace wsprrypi {
CatalogStorage sqlite_catalog_storage(std::shared_ptr<SqliteStateStore> store) {
    if(!store)throw std::invalid_argument("Catalog database required");
    return {
        [store]() -> std::optional<std::string> {
            return store->record(StateRecord::Catalog).bytes;
        },
        [store](const std::optional<std::string> &expected,const std::string &after) {
            const auto saved=store->record(StateRecord::Catalog);
            if(saved.bytes!=expected)throw std::runtime_error("Catalog revision conflict");
            try{store->commit(saved.revision,{{StateRecord::Catalog,after}});}
            catch(const StateStorageError &error){
                if(error.code()==StateStorageError::Code::Conflict)throw std::runtime_error("Catalog revision conflict");
                throw;
            }
        }
    };
}
} // namespace wsprrypi
