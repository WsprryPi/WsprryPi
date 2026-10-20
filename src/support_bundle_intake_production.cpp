#include "support_bundle_intake_production.hpp"

#include "support_bundle_intake_compiled_trust.hpp"

namespace {

bool durability_confirmed(SupportBundleIntakeStateCommitStatus status) {
    return status == SupportBundleIntakeStateCommitStatus::committed ||
           status == SupportBundleIntakeStateCommitStatus::unchanged;
}

bool identity_present(const SupportBundleIntakeManifest &manifest) {
    return manifest.generation > 0 && !manifest.published_at.empty() &&
           !manifest.expires_at.empty() && manifest.minimum_client_protocol > 0 &&
           !manifest.minimum_upload_version.empty() && !manifest.release_url.empty() &&
           !manifest.signing_key_id.empty() &&
           !manifest.bundle_encryption_key_id.empty() &&
           !manifest.manifest_sha256.empty();
}

bool manifest_empty(const SupportBundleIntakeManifest &manifest) {
    return manifest.generation == 0 && manifest.published_at.empty() &&
           manifest.expires_at.empty() && manifest.status.empty() &&
           manifest.minimum_client_protocol == 0 &&
           manifest.minimum_upload_version.empty() && !manifest.request_url &&
           manifest.release_url.empty() && !manifest.user_message &&
           manifest.bundle_encryption_key_id.empty() &&
           manifest.manifest_sha256.empty() && manifest.signing_key_id.empty();
}

SupportBundleIntakeProductionResult translate(
    const SupportBundleIntakeRuntimeResult &runtime) {
    if (!runtime.completed()) return {};
    const auto &controller = runtime.controller;
    if (controller.ready()) {
        const auto &manifest = controller.manifest;
        if ((controller.state_load_status != SupportBundleIntakeStateLoadStatus::loaded &&
             controller.state_load_status != SupportBundleIntakeStateLoadStatus::absent) ||
            controller.retrieval_failure != SupportBundleIntakeRetrievalFailure::none ||
            controller.validation_failure != SupportBundleIntakeFailure::none ||
            !durability_confirmed(controller.state_commit_status) ||
            controller.upgrade || !identity_present(manifest))
            return {};
        if (manifest.status == "active") {
            if (!manifest.request_url || manifest.minimum_upload_version.empty())
                return {};
            SupportBundleIntakeProductionResult result;
            result.status = SupportBundleIntakeProductionStatus::active;
            result.generation = manifest.generation;
            result.expires_at = manifest.expires_at;
            result.minimum_upload_version = manifest.minimum_upload_version;
            result.signing_key_id = manifest.signing_key_id;
            result.bundle_key_id = manifest.bundle_encryption_key_id;
            result.request_url = manifest.request_url;
            result.user_message = manifest.user_message;
            return result;
        }
        if (manifest.status == "disabled" && !manifest.request_url) {
            SupportBundleIntakeProductionResult result;
            result.status = SupportBundleIntakeProductionStatus::disabled;
            result.generation = manifest.generation;
            result.expires_at = manifest.expires_at;
            result.signing_key_id = manifest.signing_key_id;
            result.bundle_key_id = manifest.bundle_encryption_key_id;
            result.user_message = manifest.user_message;
            return result;
        }
        return {};
    }
    if (controller.failure != SupportBundleIntakeControllerFailure::validation_failed ||
        (controller.state_load_status != SupportBundleIntakeStateLoadStatus::loaded &&
         controller.state_load_status != SupportBundleIntakeStateLoadStatus::absent) ||
        controller.retrieval_failure != SupportBundleIntakeRetrievalFailure::none ||
        controller.validation_failure != SupportBundleIntakeFailure::upgrade_required ||
        !durability_confirmed(controller.state_commit_status) ||
        !controller.upgrade || !manifest_empty(controller.manifest))
        return {};
    const auto &upgrade = *controller.upgrade;
    if (upgrade.minimum_upload_version.empty() || upgrade.release_url.empty())
        return {};
    SupportBundleIntakeProductionResult result;
    result.status = SupportBundleIntakeProductionStatus::upgrade_required;
    result.minimum_upload_version = upgrade.minimum_upload_version;
    result.release_url = upgrade.release_url;
    result.user_message = upgrade.user_message;
    return result;
}

} // namespace

SupportBundleIntakeProductionResult resolve_support_bundle_intake_production() {
    return resolve_support_bundle_intake_production_for_test([] {
        return resolve_support_bundle_intake_runtime(
            support_bundle_intake_compiled_trust());
    });
}

SupportBundleIntakeProductionResult resolve_support_bundle_intake_production_for_test(
    const SupportBundleIntakeRuntimeProvider &provider) {
    try {
        if (!provider) return {};
        return translate(provider());
    } catch (...) {
        return {};
    }
}
