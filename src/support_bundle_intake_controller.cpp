#include "support_bundle_intake_controller.hpp"

namespace {

SupportBundleIntakeControllerResult resolve_internal(
    const SupportBundleIntakeControllerRequest &request,
    const SupportBundleIntakeControllerDependencies &dependencies) {
    SupportBundleIntakeControllerResult result;
    if (!dependencies.load_state || !dependencies.retrieve || !dependencies.validate ||
        !dependencies.commit_state)
        return result;

    const auto loaded = dependencies.load_state(request.state_root);
    result.state_load_status = loaded.status;
    if (loaded.status != SupportBundleIntakeStateLoadStatus::loaded &&
        loaded.status != SupportBundleIntakeStateLoadStatus::absent)
        return result;

    const auto retrieved = dependencies.retrieve(request.retrieval);
    result.retrieval_failure = retrieved.failure;
    if (!retrieved.retrieved()) {
        result.failure = SupportBundleIntakeControllerFailure::retrieval_failed;
        return result;
    }

    SupportBundleIntakeValidationRequest validation;
    validation.manifest_bytes = retrieved.manifest_bytes;
    validation.signature_envelope_bytes = retrieved.signature_envelope_bytes;
    validation.signing_keys = request.signing_keys;
    validation.recognized_bundle_key_ids = request.recognized_bundle_key_ids;
    validation.installed_upload_version = request.installed_upload_version;
    validation.now_utc_seconds = request.now_utc_seconds;
    validation.client_protocol = request.client_protocol;
    if (loaded.loaded()) {
        validation.previous = SupportBundleIntakePreviousState{
            loaded.state.generation, loaded.state.manifest_sha256};
    }
    const auto validated = dependencies.validate(validation);
    result.validation_failure = validated.failure;
    if (!validated.valid()) {
        result.failure = SupportBundleIntakeControllerFailure::validation_failed;
        if (validated.failure != SupportBundleIntakeFailure::upgrade_required ||
            !validated.upgrade || !validated.accepted_state)
            return result;
        const auto committed = dependencies.commit_state(
            request.state_root,
            {validated.accepted_state->generation,
             validated.accepted_state->manifest_sha256});
        result.state_commit_status = committed.status;
        if (committed.status == SupportBundleIntakeStateCommitStatus::committed_sync_uncertain) {
            result.failure = SupportBundleIntakeControllerFailure::state_durability_uncertain;
            return result;
        }
        if (!committed.durability_confirmed()) {
            result.failure = SupportBundleIntakeControllerFailure::state_commit_failed;
            return result;
        }
        result.upgrade = validated.upgrade;
        return result;
    }

    const auto committed = dependencies.commit_state(
        request.state_root,
        {validated.manifest.generation, validated.manifest.manifest_sha256});
    result.state_commit_status = committed.status;
    if (committed.status == SupportBundleIntakeStateCommitStatus::committed_sync_uncertain) {
        result.failure = SupportBundleIntakeControllerFailure::state_durability_uncertain;
        return result;
    }
    if (!committed.durability_confirmed()) {
        result.failure = SupportBundleIntakeControllerFailure::state_commit_failed;
        return result;
    }

    result.failure = SupportBundleIntakeControllerFailure::none;
    result.manifest = validated.manifest;
    return result;
}

SupportBundleIntakeControllerDependencies production_dependencies() {
    return {
        load_support_bundle_intake_state,
        retrieve_support_bundle_intake,
        validate_support_bundle_intake,
        commit_support_bundle_intake_state,
    };
}

} // namespace

SupportBundleIntakeControllerResult resolve_support_bundle_intake(
    const SupportBundleIntakeControllerRequest &request) {
    return resolve_internal(request, production_dependencies());
}

SupportBundleIntakeControllerResult resolve_support_bundle_intake_for_test(
    const SupportBundleIntakeControllerRequest &request,
    const SupportBundleIntakeControllerDependencies &dependencies) {
    return resolve_internal(request, dependencies);
}
