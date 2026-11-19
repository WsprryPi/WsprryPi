// SPDX-License-Identifier: MIT
// Retirement preserves the native management reservation contract without
// constructing an execution client or transport.
#include "wtp_managed_output.hpp"
#include "wtp_runtime_bridge.hpp"
#include <iostream>
#include <thread>
#include <stdexcept>
using namespace wsprrypi;
#define CHECK(x) do {if(!(x))throw std::runtime_error(#x);}while(false)
int main(){try{
  const auto initial=wtp_runtime_managed_output();CHECK(initial&&!initial->unknown&&!initial->ready&&initial->cancellable);
  CHECK(!initial->destination_id&&!initial->output.owner_id&&!initial->output.job_id);
  CHECK(!wtp_runtime_selected());
  auto stale=initial->output;++stale.generation;
  CHECK(!initial->reserve_stop(stale));
  CHECK(initial->reserve_configuration());CHECK(!wtp_runtime_switch_allowed());
  CHECK(wtp_runtime_selection_error(std::nullopt).empty());
  std::thread observer([]{const auto held=wtp_runtime_managed_output();CHECK(held&&!held->reserve_stop&&!held->reserve_configuration);CHECK(!wtp_runtime_selection_error(std::nullopt).empty());});observer.join();
  CHECK(!initial->reserve_stop(initial->output));
  initial->release_configuration();CHECK(wtp_runtime_switch_allowed());
  const auto fresh=wtp_runtime_managed_output();CHECK(fresh&&fresh->reserve_stop(fresh->output));
  initial->release_configuration(); // A stale release cannot release a successor.
  CHECK(!wtp_runtime_switch_allowed());
  const auto stopped=wtp_runtime_managed_output();CHECK(stopped&&stopped->stop_reserved&&!stopped->reserve_configuration);
  fresh->release_stop();CHECK(wtp_runtime_switch_allowed());
  CHECK(wtp_runtime_selection_error(WtpSettings{}).find("Migration")!=std::string::npos);
  CHECK(!wtp_runtime_recover().ok&&!wtp_runtime_cancel_job(std::string(32,'a')).ok);
  CHECK(wtp_runtime_management("status","GET",{},{},{}).status==409);
  std::cout<<"PASS native management reservations, stale-release fencing, no execution owner or remote cleanup\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
