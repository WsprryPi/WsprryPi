// SPDX-License-Identifier: MIT
#pragma once
#include "pi_pair_client.hpp"
#include "pi_pair_history.hpp"
#include "browser_api.hpp"
#include "catalog.hpp"
#include "pico_management_journal.hpp"
#include <map>
namespace wsprrypi {
// Controller-side authenticated observations. The enrolled owner supplies the
// only endpoint. This class never executes WTP or constructs a native host.
class PiPairRemoteController {
public:
    using History=std::function<std::shared_ptr<PiPairHistory>()>;
    using Active=std::function<std::optional<WtpSettings>()>;
    PiPairRemoteController(History,PiPairAdmission &,std::string self,WtpCatalog &,std::string directory,Active = {});
    struct LoopbackTest {PiPairEndpoint endpoint;};
    PiPairRemoteController(History,PiPairAdmission &,std::string self,WtpCatalog &,std::string directory,LoopbackTest,Active = {});
    // One selected profile per bounded worker step; no polling request fans out
    // to 64 connections. Successful observations are RAM hints, not authority.
    using AuthoritySource=std::function<PicoHttpResponse()>;
    // The existing controller retirement owner supplies its real closed book.
    // One maintenance turn observes inventory or registers/reconciles history;
    // it never combines network exchanges or dispatches configuration/output.
    void authority_source(AuthoritySource);
    void receipt_eviction_observer(std::function<void()>);
    bool maintain(const std::string &profile,const std::function<bool()> &cancelled=[] {return false;});
    bool observe(const std::string &profile,const std::function<bool()> &cancelled=[] {return false;});
    PicoHttpResponse targets();
    using AuthorizationObserver=std::function<void(const std::string &,const PiPairPrincipal &,const nlohmann::json &)>;
    // Runtime-owned observer sees only strictly validated authenticated contexts.
    void authorization_observer(AuthorizationObserver);
    // Resolves only current paired inventory. A known but unavailable remote
    // identity fails closed rather than falling through to another host.
    std::optional<std::string> resolve(const std::string &device,const std::string &profile_hint = {});
    PiPairHttpReply read(const BrowserRequest &,const std::string &profile,const std::function<bool()> &cancelled=[] {return false;},
        const std::optional<std::string> &browser_context = {});
    // Existing native Pi commands only; a durable shared reservation precedes
    // the first TLS handoff. This is not a retry or scheduling dispatcher.
    PiPairHttpReply write(const BrowserRequest &,const std::string &profile,const std::function<bool()> &cancelled=[] {return false;});
    PiPairHttpReply write_pico(const BrowserRequest &,const std::string &profile,const std::string &context,
        const std::string &operation,const std::function<bool()> &cancelled=[] {return false;});
    PiPairHttpReply review(const BrowserRequest &,const std::string &profile,const std::function<bool()> &cancelled=[] {return false;});
private:
    struct Selection {
        std::string profile,generation,target;
        PiPairTlsIdentity identity;
        PiPairEndpoint endpoint;
        bool operator==(const Selection &) const;
    };
    struct Entry {std::mutex operation,publication,registration;bool authority_turn{},registration_absence{},registration_context_ready{};std::optional<Selection> registration_selection;std::string registration_operation,resume_confirmed;std::optional<Selection> resume_selection;unsigned registration_failures{};std::chrono::steady_clock::time_point registration_next{};Selection selection; nlohmann::json targets=nlohmann::json::array();
        std::map<std::string,nlohmann::json> contexts;
        std::map<std::string,std::string> wire_contexts;
        std::map<std::string,std::string> public_contexts;
        std::map<std::string,std::uint64_t> epochs;
        struct Readback {std::string resource,token,wire_context;nlohmann::json binding;std::chrono::steady_clock::time_point at;};
        std::map<std::string,Readback> reads;
        struct Recovery {std::string operation,resource;};
        std::map<std::string,Recovery> recoveries;
        std::chrono::steady_clock::time_point observed{},attempted{};bool attempted_once{};};
    // Request-local cache image. Only metadata publication takes the parent
    // lock; per-device admission owns network work independently of siblings.
    struct ContextLease {
        std::shared_ptr<Entry> published,selected;
        std::string device;
        nlohmann::json previous_target;
        ContextLease(std::shared_ptr<Entry>,std::string);
        ~ContextLease();
    };
    Selection select(PiPairHistory &,const std::string &);
    void validate_inventory(PiPairHistory &,const std::string &profile,const nlohmann::json &targets);
    std::shared_ptr<Entry> entry(const std::string &);
    std::shared_ptr<PicoManagementJournal> journal(const std::string &device);
    enum class ResumeTurn {Ready,Advanced,Held};
    ResumeTurn coordinate_resume(const std::string &,const std::function<bool()> &);
    bool register_authority(const std::string &,AuthoritySource,const std::function<bool()> &);
    PiPairClientResult exchange(PiPairHistory &,const Selection &,const std::string &,const std::function<bool()> &,const httplib::Headers & = {});
    WtpCatalog &catalog_;
    Active active_;
    History history_;
    std::string self_,directory_;
    PiPairClient client_;
    std::optional<LoopbackTest> test_;
    std::mutex publication_mutex_;
    std::mutex entries_mutex_;
    std::map<std::string,std::shared_ptr<Entry>> entries_;
    std::mutex observer_mutex_;
    AuthorizationObserver observer_;
    AuthoritySource authority_source_;
    std::function<void()> retire_contexts_;
    std::mutex journals_mutex_;
    std::map<std::string,std::shared_ptr<PicoManagementJournal>> journals_;
};
}
