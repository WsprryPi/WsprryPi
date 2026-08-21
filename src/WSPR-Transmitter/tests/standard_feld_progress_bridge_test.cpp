#include "execution_plan_compiler.hpp"
#include "rpi_standard_feld_progress_bridge.hpp"
#include <cstdlib>
#include <iostream>
#include <new>
#include <future>
using namespace wsprrypi;
static void require(bool ok, const char* label) { if (!ok) { std::cerr << "FAIL: " << label << '\n'; std::exit(1); } }
static ExecutionPlan make_plan() { TransmissionRequest r; r.id.value=71; r.mode=TransmissionMode::STANDARD_FELD; r.output.backend=BackendKind::RPI_CLOCK_GPIO; r.payload=StandardFeldPayload{"A",14097000.0}; return ExecutionPlanCompiler{}.compile(r); }
int main() {
 auto plan=make_plan(); RpiStandardFeldProgressBridge bridge;
 RpiStandardFeldExecutionResult cancelled; cancelled.terminal=RpiStandardFeldExecutionTerminal::CANCELLED;
 RpiStandardFeldExecutionResult failed; failed.terminal=RpiStandardFeldExecutionTerminal::FAILED;
#ifdef STANDARD_FELD_PROGRESS_TEST_HOOKS
 struct HookRelease { ~HookRelease(){ release_rpi_standard_feld_progress_pause(); clear_rpi_standard_feld_progress_test_fault(); } };
 const auto blocked=[](auto& future){ return future.wait_for(std::chrono::milliseconds{1})==std::future_status::timeout; };
 // H02 CLEAR before mutation.
 require(bridge.prepare(plan,70),"H02 setup"); arm_rpi_standard_feld_progress_pause(ProgressTestOperation::CLEAR,ProgressTestPhase::AFTER_LOCK_BEFORE_MUTATION); HookRelease h02;
 auto h02clear=std::async(std::launch::async,[&]{bridge.clear();}); require(wait_rpi_standard_feld_progress_pause(),"H02 clear paused"); auto h02snap=std::async(std::launch::async,[&]{return bridge.snapshot();}); require(blocked(h02snap),"H02 snapshot blocks"); release_rpi_standard_feld_progress_pause(); h02clear.get(); require(h02snap.get().state==RpiStandardFeldProgressState::EMPTY,"H02 empty after release");
 // H03 CLEAR after mutation.
 require(bridge.prepare(plan,71),"H03 setup"); arm_rpi_standard_feld_progress_pause(ProgressTestOperation::CLEAR,ProgressTestPhase::AFTER_MUTATION_BEFORE_UNLOCK); auto h03clear=std::async(std::launch::async,[&]{bridge.clear();}); require(wait_rpi_standard_feld_progress_pause(),"H03 clear mutated pause"); auto h03report=std::async(std::launch::async,[&]{return bridge.report(71,0,*plan.events[0].raster_progress);}); require(blocked(h03report),"H03 report blocks"); release_rpi_standard_feld_progress_pause(); h03clear.get(); require(!h03report.get()&&bridge.snapshot().state==RpiStandardFeldProgressState::EMPTY,"H03 report rejects empty");
 // H04 TERMINAL before mutation.
 require(bridge.prepare(plan,72),"H04 setup"); arm_rpi_standard_feld_progress_pause(ProgressTestOperation::TERMINAL,ProgressTestPhase::AFTER_LOCK_BEFORE_MUTATION); auto h04term=std::async(std::launch::async,[&]{return bridge.finalize(72,cancelled);}); require(wait_rpi_standard_feld_progress_pause(),"H04 terminal paused"); auto h04report=std::async(std::launch::async,[&]{return bridge.report(72,0,*plan.events[0].raster_progress);}); require(blocked(h04report),"H04 report blocks"); release_rpi_standard_feld_progress_pause(); require(h04term.get()&&!h04report.get(),"H04 cancelled rejects report");
 // H05 TERMINAL after mutation.
 require(bridge.prepare(plan,73),"H05 setup"); arm_rpi_standard_feld_progress_pause(ProgressTestOperation::TERMINAL,ProgressTestPhase::AFTER_MUTATION_BEFORE_UNLOCK); auto h05term=std::async(std::launch::async,[&]{return bridge.finalize(73,failed);}); require(wait_rpi_standard_feld_progress_pause(),"H05 terminal mutated pause"); auto h05snap=std::async(std::launch::async,[&]{return bridge.snapshot();}); require(blocked(h05snap),"H05 snapshot blocks"); release_rpi_standard_feld_progress_pause(); h05term.get(); require(h05snap.get().state==RpiStandardFeldProgressState::FAILED,"H05 coherent failed snapshot");
 // H06 PREPARE_INSTALL before mutation.
 require(bridge.prepare(plan,74),"H06 A setup"); require(bridge.report(74,0,*plan.events[0].raster_progress),"H06 A prefix"); arm_rpi_standard_feld_progress_pause(ProgressTestOperation::PREPARE_INSTALL,ProgressTestPhase::AFTER_LOCK_BEFORE_MUTATION); auto h06install=std::async(std::launch::async,[&]{return bridge.prepare(plan,75);}); require(wait_rpi_standard_feld_progress_pause(),"H06 install paused"); auto h06snap=std::async(std::launch::async,[&]{return bridge.snapshot();}); require(blocked(h06snap),"H06 snapshot blocks"); release_rpi_standard_feld_progress_pause(); require(h06install.get()&&h06snap.get().generation==75&&!bridge.report(74,1,*plan.events[1].raster_progress),"H06 B active stale A rejected");
 // H07 PREPARE_INSTALL after mutation.
 require(bridge.prepare(plan,76),"H07 A setup"); arm_rpi_standard_feld_progress_pause(ProgressTestOperation::PREPARE_INSTALL,ProgressTestPhase::AFTER_MUTATION_BEFORE_UNLOCK); auto h07install=std::async(std::launch::async,[&]{return bridge.prepare(plan,77);}); require(wait_rpi_standard_feld_progress_pause(),"H07 install mutated pause"); auto h07snap=std::async(std::launch::async,[&]{return bridge.snapshot();}); require(blocked(h07snap),"H07 snapshot blocks"); release_rpi_standard_feld_progress_pause(); h07install.get(); const auto h07state=h07snap.get(); require(h07state.generation==77&&h07state.completed.empty(),"H07 coherent empty B");
 // H08 SNAPSHOT before capture.
 require(bridge.prepare(plan,78),"H08 setup"); arm_rpi_standard_feld_progress_pause(ProgressTestOperation::SNAPSHOT,ProgressTestPhase::AFTER_LOCK_BEFORE_MUTATION); auto h08snap=std::async(std::launch::async,[&]{return bridge.snapshot();}); require(wait_rpi_standard_feld_progress_pause(),"H08 snapshot paused"); auto h08report=std::async(std::launch::async,[&]{return bridge.report(78,0,*plan.events[0].raster_progress);}); require(blocked(h08report),"H08 report blocks"); release_rpi_standard_feld_progress_pause(); require(h08snap.get().completed.empty()&&h08report.get(),"H08 captured prior coherent snapshot");
 // H09 SNAPSHOT after capture.
 require(bridge.prepare(plan,79),"H09 setup"); arm_rpi_standard_feld_progress_pause(ProgressTestOperation::SNAPSHOT,ProgressTestPhase::AFTER_MUTATION_BEFORE_UNLOCK); auto h09snap=std::async(std::launch::async,[&]{return bridge.snapshot();}); require(wait_rpi_standard_feld_progress_pause(),"H09 snapshot captured pause"); auto h09report=std::async(std::launch::async,[&]{return bridge.report(79,0,*plan.events[0].raster_progress);}); require(blocked(h09report),"H09 report blocks"); release_rpi_standard_feld_progress_pause(); require(h09snap.get().completed.empty()&&h09report.get()&&bridge.snapshot().completed.size()==1,"H09 captured immutable then later report");
 // H10 completed terminal after mutation.
 RpiStandardFeldExecutionResult h10complete; h10complete.terminal=RpiStandardFeldExecutionTerminal::COMPLETED; h10complete.safe_idle_confirmed=true;
 require(bridge.prepare(plan,80),"H10 setup"); for(std::size_t i=0;i<plan.events.size();++i) require(bridge.report(80,i,*plan.events[i].raster_progress),"H10 full history"); arm_rpi_standard_feld_progress_pause(ProgressTestOperation::TERMINAL,ProgressTestPhase::AFTER_MUTATION_BEFORE_UNLOCK); auto h10term=std::async(std::launch::async,[&]{return bridge.finalize(80,h10complete);}); require(wait_rpi_standard_feld_progress_pause(),"H10 completion mutated pause"); auto h10report=std::async(std::launch::async,[&]{return bridge.report(80,0,*plan.events[0].raster_progress);}); require(blocked(h10report),"H10 report blocks"); release_rpi_standard_feld_progress_pause(); require(h10term.get()&&!h10report.get()&&bridge.snapshot().state==RpiStandardFeldProgressState::COMPLETED,"H10 completed rejects report");
 require(bridge.prepare(plan,59),"H01 hook setup");
 arm_rpi_standard_feld_progress_pause(ProgressTestOperation::REPORT,ProgressTestPhase::AFTER_LOCK_BEFORE_MUTATION);
 auto paused_report=std::async(std::launch::async,[&]{return bridge.report(59,0,*plan.events[0].raster_progress);});
 require(wait_rpi_standard_feld_progress_pause(),"H01 report owns progress mutex");
 auto competing_snapshot=std::async(std::launch::async,[&]{return bridge.snapshot();});
 require(competing_snapshot.wait_for(std::chrono::milliseconds{1})==std::future_status::timeout,"H01 snapshot waits for owned mutex");
 release_rpi_standard_feld_progress_pause();
 require(paused_report.get()&&competing_snapshot.get().completed.size()==1,"H01 release completes report and snapshot");
 require(bridge.prepare(plan,60),"G01 capacity setup"); const auto cap=bridge.completed_capacity_for_test();
 require(cap>=plan.events.size()&&bridge.snapshot().completed.empty(),"G01 capacity reserved before reports");
 for(std::size_t i=0;i<plan.events.size();++i) require(bridge.report(60,i,*plan.events[i].raster_progress)&&bridge.snapshot().completed.size()==i+1&&bridge.completed_capacity_for_test()==cap,"G02 every accepted report retains capacity");
 require(!bridge.report(60,plan.events.size(),*plan.events.back().raster_progress)&&bridge.snapshot().completed.size()==plan.events.size()&&bridge.completed_capacity_for_test()==cap,"G03 overflow rejection retains capacity");
 require(!bridge.report(60,0,*plan.events[0].raster_progress)&&bridge.completed_capacity_for_test()==cap,"G03 duplicate rejection retains capacity");
 RpiStandardFeldExecutionResult capacity_complete; capacity_complete.terminal=RpiStandardFeldExecutionTerminal::COMPLETED; capacity_complete.safe_idle_confirmed=true;
 require(bridge.finalize(60,capacity_complete)&&bridge.snapshot().state==RpiStandardFeldProgressState::COMPLETED&&bridge.completed_capacity_for_test()==cap,"G04 completion retains capacity");
 require(!bridge.report(60,0,*plan.events[0].raster_progress)&&bridge.completed_capacity_for_test()==cap,"G04 post-terminal rejection retains capacity");
 auto allocation_failure=[&](RpiStandardFeldProgressTestFault fault, std::size_t after, const char* label) {
   require(bridge.prepare(plan,50),"A00 baseline install"); require(bridge.report(50,0,*plan.events[0].raster_progress),"A00 baseline prefix");
   const auto before=bridge.snapshot(); set_rpi_standard_feld_progress_test_fault(fault,after);
   require(!bridge.prepare(plan,51),label); clear_rpi_standard_feld_progress_test_fault(); const auto after_snapshot=bridge.snapshot();
   require(after_snapshot.generation==before.generation&&after_snapshot.state==before.state&&after_snapshot.completed.size()==before.completed.size(),"A00 allocation failure preserves snapshot");
   require(bridge.report(50,1,*plan.events[1].raster_progress),"A00 prior generation remains usable");
 };
 allocation_failure(RpiStandardFeldProgressTestFault::EXPECTED_RESERVE,0,"A01 expected reserve failure");
 allocation_failure(RpiStandardFeldProgressTestFault::COMPLETED_RESERVE,0,"A02 completed reserve failure");
 allocation_failure(RpiStandardFeldProgressTestFault::EXPECTED_COPY,2,"A03 expected copy failure");
 allocation_failure(RpiStandardFeldProgressTestFault::BEFORE_INSTALL,0,"A04 before-install failure");
 require(bridge.prepare(plan,52),"A05 report setup"); const auto before_append=bridge.snapshot();
 set_rpi_standard_feld_progress_test_fault(RpiStandardFeldProgressTestFault::REPORT_APPEND); require(!bridge.report(52,0,*plan.events[0].raster_progress),"A05 report append failure controlled"); clear_rpi_standard_feld_progress_test_fault();
 require(bridge.snapshot().completed.size()==before_append.completed.size()&&bridge.report(52,0,*plan.events[0].raster_progress),"A05 report retry succeeds unchanged");
 set_rpi_standard_feld_progress_test_fault(RpiStandardFeldProgressTestFault::SNAPSHOT_COPY); bool snapshot_threw=false; try { (void)bridge.snapshot(); } catch(const std::bad_alloc&) { snapshot_threw=true; } clear_rpi_standard_feld_progress_test_fault();
 require(snapshot_threw&&bridge.snapshot().completed.size()==1,"A06 snapshot allocation failure preserves state and unlocks");
#endif
 require(bridge.prepare(plan,6),"S00 valid installed snapshot");
 require(bridge.report(6,0,*plan.events[0].raster_progress),"S00 installed prefix");
 const auto baseline=bridge.snapshot();
 const auto unchanged=[&](const char* label, const ExecutionPlan& candidate) {
   require(!bridge.prepare(candidate,7),label); const auto after=bridge.snapshot();
   require(after.state==baseline.state&&after.generation==baseline.generation&&after.plan_id.value==baseline.plan_id.value&&after.total_positions==baseline.total_positions&&after.completed.size()==baseline.completed.size()&&after.completed[0].event_index==baseline.completed[0].event_index,"S00 rejected replacement preserves complete observable snapshot");
 };
 auto bad=plan; bad.backend=BackendKind::SI5351; unchanged("S01 wrong backend",bad);
 bad=plan; bad.mode=TransmissionMode::WSPR; unchanged("S02 wrong mode",bad);
 bad=plan; bad.events[0].raster_progress.reset(); unchanged("S03 missing RasterProgress metadata",bad);
 bad=plan; bad.events[1].raster_progress->absolute_position++; unchanged("S04 wrong absolute position",bad);
 bad=plan; bad.events[100].raster_progress->normalized_char_index++; unchanged("S05 wrong normalized character index",bad);
 bad=plan; bad.events[100].raster_progress->cell_kind=RfEvent::RasterProgress::CellKind::LEADER; unchanged("S06 wrong cell kind",bad);
 bad=plan; bad.events[1].raster_progress->cell_column++; unchanged("S07 wrong cell column",bad);
 bad=plan; bad.events[1].raster_progress->physical_position++; unchanged("S08 wrong physical position",bad);
 bad=plan; bad.events[100].message_char_index++; unchanged("S09 event message index disagrees",bad);
 bad=plan; bad.events[0].raster_progress->cell_kind=RfEvent::RasterProgress::CellKind::MESSAGE; unchanged("S10 malformed leader kind",bad);
 bad=plan; bad.events[0].raster_progress->normalized_char_index=0; unchanged("S11 malformed leader sentinel",bad);
 bad=plan; bad.events.back().raster_progress->cell_kind=RfEvent::RasterProgress::CellKind::MESSAGE; unchanged("S12 malformed trailer kind",bad);
 bad=plan; bad.events.back().raster_progress->normalized_char_index=0; unchanged("S13 malformed trailer sentinel",bad);
 bad=plan; bad.events[100].raster_progress->cell_kind=RfEvent::RasterProgress::CellKind::TRAILER; unchanged("S14 malformed interior message kind",bad);
 bad=plan; bad.events[80].raster_progress->normalized_char_index+=2; unchanged("S15 skipped message index",bad);
 bad=plan; bad.events.pop_back(); unchanged("S16 declared total inconsistent expected positions",bad);
 bad=plan; ++bad.summary.event_count; unchanged("S17 summary event count inconsistent",bad);
 bad=plan; bad.events.resize(293); unchanged("S18 incomplete cell count",bad);
 bad=plan; bad.events.resize(2); bad.summary.event_count=2; unchanged("S19 insufficient cells",bad);
 bad=plan; bad.reference_frequency_hz=0; unchanged("S20 invalid reference carrier",bad);
 bad=plan; bad.events[1].frequency_hz+=1; unchanged("S21 per-event carrier mismatch",bad);
 bad=plan; bad.events[1].offset_from_start+=std::chrono::nanoseconds{1}; unchanged("S22 altered offset",bad);
 require(!bridge.prepare(plan,0),"S01 invalid generation rejected");
 auto wrong=plan; wrong.mode=TransmissionMode::WSPR; require(!bridge.prepare(wrong,1),"S02 wrong mode rejected");
 wrong=plan; wrong.backend=BackendKind::SI5351; require(!bridge.prepare(wrong,1),"S03 wrong backend rejected");
 wrong=plan; ++wrong.summary.event_count; require(!bridge.prepare(wrong,1),"S03 summary/declared total mismatch rejected");
 require(bridge.prepare(plan,7),"B01 real bridge installs immutable identity");
 for(std::size_t i=0;i<plan.events.size();++i) require(bridge.report(7,i,*plan.events[i].raster_progress),"B02 real bridge report");
 RpiStandardFeldExecutionResult completed; completed.terminal=RpiStandardFeldExecutionTerminal::COMPLETED; completed.safe_idle_confirmed=true;
 require(bridge.finalize(7,completed)&&bridge.snapshot().state==RpiStandardFeldProgressState::COMPLETED,"B03 successful completion maps COMPLETED");
 require(bridge.prepare(plan,8),"M30 setup"); for(std::size_t i=0;i<plan.events.size();++i) require(bridge.report(8,i,*plan.events[i].raster_progress),"M30 applied position");
 completed.safe_idle_confirmed=false; require(bridge.finalize(8,completed)&&bridge.snapshot().state==RpiStandardFeldProgressState::FAILED,"M30 cleanup-converted completion maps FAILED");
 require(bridge.prepare(plan,9),"M31 setup"); require(bridge.report(9,0,*plan.events[0].raster_progress),"M31 prefix"); RpiStandardFeldExecutionResult wd; wd.terminal=RpiStandardFeldExecutionTerminal::FAILED; wd.watchdog_faulted=true;
 require(bridge.finalize(9,wd)&&bridge.snapshot().state==RpiStandardFeldProgressState::WATCHDOG_FAULT&&bridge.snapshot().completed.size()==1,"M31 watchdog maps WATCHDOG_FAULT");
 require(bridge.prepare(plan,10),"T01 cancellation setup"); require(bridge.report(10,0,*plan.events[0].raster_progress),"T01 cancellation prefix");
 require(bridge.finalize(10,cancelled)&&bridge.snapshot().state==RpiStandardFeldProgressState::CANCELLED&&!bridge.report(10,1,*plan.events[1].raster_progress),"T01 cancellation maps CANCELLED and rejects later report");
 require(!bridge.finalize(10,completed),"T01 cancellation cannot become completion");
 require(bridge.prepare(plan,11),"T02 failure setup"); require(bridge.report(11,0,*plan.events[0].raster_progress),"T02 failure prefix");
 require(bridge.finalize(11,failed)&&bridge.snapshot().state==RpiStandardFeldProgressState::FAILED&&!bridge.report(11,1,*plan.events[1].raster_progress),"T02 ordinary failure maps FAILED and rejects later report");
 completed.safe_idle_confirmed=true;
 require(!bridge.finalize(11,completed),"T02 failure cannot become completion");
 std::cout<<"PASS: Standard Feld production progress bridge\n";
}
