// SPDX-License-Identifier: GPL-3.0-or-later
#include "reconciliation.hpp"
#include "legacy.hpp"
#include <cerrno>

namespace athena::activation {
namespace {
bool missing(const ProjectionError& e) {
    return e.cause == ProjectionCause::MissingWitness || e.cause == ProjectionCause::MissingManaged;
}
}
void ExecutionAccess::witness(const ProjectionContext& p, std::string_view path, const Link& link, bool sync) {
    auto dir = std::holds_alternative<ActivationId>(link.witness.owner) ? p.container(p.witnesses_.get(),link.witness,false) :
        LegacyAccess::resolve(p,path,link.target,link.witness);
    p.link_identity(dir.get(),std::to_string(link.witness.entry_id),link.target,ProjectionCause::MissingWitness);
    if(sync) p.barrier(dir.get(),true);
}
void ExecutionAccess::verify_complete(const ProjectionContext& p, const Authorized& a, bool sync) {
    require_selected(p,a);
    for (const auto& [path, link] : a.desired_links) {
        p.validate_owned_symlink(path,link.target,link.witness);
        if (sync) {
            auto [parent, leaf] = p.parent(path); p.barrier(parent.get(),true);
        }
    }
    for (const auto& path : a.desired_directories) directory(p,path,sync);
    for (const auto& [path, link] : a.old_links) {
        witness(p,path,link,sync);
        if (!a.desired_links.contains(path) && !a.desired_directories.contains(path)) absent(p,path,sync);
    }
    p.revalidate(); require_selected(p,a);
}
ProjectionOutcome ExecutionAccess::apply(ProjectionContext& p, const ProjectionState& state, const Authorized& a) {
    p.state(state); require_selected(p,a);
    // A visible post-rename current may still have uncertain durability. Establish
    // its current-instance barriers before touching the projection, independently
    // of whether the caller previously recovered D6.
    (void)current(p,true); require_selected(p,a);
    bool progressed=false;
    try {
        for (const auto& op : a.document.plan.operations) {
            require_selected(p,a); p.revalidate();
            std::visit([&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T,RemoveLink>) {
                    const auto& old=a.old_links.at(op.path);
                    if(a.desired_directories.contains(op.path)) {
                        try {
                            directory(p,op.path,true); witness(p,op.path,old,true);
                            return; // REMOVE followed by ENSURE_DIRECTORY already reached its final state.
                        } catch(const ProjectionError& e) {
                            if(e.cause!=ProjectionCause::UnsafeAncestor ||
                                (e.error!=ENOENT && e.error!=ENOTDIR && e.error!=ELOOP)) throw;
                        }
                    }
                    p.remove_symlink(state,op.path,old.target,old.witness);
                } else if constexpr (std::is_same_v<T,EnsureDirectory>) {
                    p.ensure_directory(state,op.path);
                } else if constexpr (std::is_same_v<T,ReplaceLink>) {
                    const auto& old=a.old_links.at(op.path); const auto& next=a.desired_links.at(op.path);
                    p.replace_symlink(state,op.path,old.target,old.witness,next.target,next.witness);
                } else if constexpr (std::is_same_v<T,CreateLink>) {
                    const auto& next=a.desired_links.at(op.path);
                    try { p.recover_symlink(state,op.path,next.target,next.witness); }
                    catch(const ProjectionError& e) {
                        if (!missing(e)) throw;
                        p.create_symlink(state,op.path,next.target,next.witness);
                    }
                } else {
                    const auto& old=a.old_links.at(op.path); const auto& next=a.desired_links.at(op.path);
                    try { p.recover_symlink(state,op.path,next.target,next.witness); }
                    catch(const ProjectionError& e) {
                        if (!missing(e)) throw;
                        p.succeed_witness(state,op.path,old.target,old.witness,next.witness);
                        p.recover_symlink(state,op.path,next.target,next.witness);
                    }
                }
            },op.state);
            progressed=true;
        }
        verify_complete(p,a,true);
        return ProjectionOutcome::MutatedDurably;
    } catch (ProjectionError& e) { if(progressed) e.outcome=ProjectionOutcome::OutcomeUncertain; throw; }
    catch (ExecutionError& e) { if(progressed) e.outcome=ProjectionOutcome::OutcomeUncertain; throw; }
    catch (const std::exception& e) {
        throw ExecutionError(ExecutionCause::Io,progressed?ProjectionOutcome::OutcomeUncertain:ProjectionOutcome::NotMutated,0,e.what());
    }
}
ReconciliationResult reconcile_selection(ProjectionContext& p, const ProjectionState& s, const PlanIdentity& id) {
    auto a=ExecutionAccess::authorize(p,s,id);
    return {ReconciliationState::Complete,ExecutionAccess::apply(p,s,a)};
}
ReconciliationResult inspect_reconciliation(ProjectionContext& p, const ProjectionState& s, const PlanIdentity& id) {
    auto a=ExecutionAccess::authorize(p,s,id); ExecutionAccess::require_selected(p,a);
    bool complete=true;
    auto blocker = [&](std::string path) {
        for (;;) {
            auto old=a.old_links.find(path);
            if(old!=a.old_links.end() && a.desired_directories.contains(path)) {
                p.validate_owned_symlink(path,old->second.target,old->second.witness);return true;
            }
            auto slash=path.rfind('/');if(slash==path.npos)return false;path.resize(slash);
        }
    };
    for(const auto& [path,link]:a.desired_links) {
        try { p.validate_owned_symlink(path,link.target,link.witness); }
        catch(const ProjectionError& e) {
            if(e.cause!=ProjectionCause::MissingWitness && e.cause!=ProjectionCause::MissingManaged &&
               e.cause!=ProjectionCause::OwnershipMismatch) throw;
            auto old=a.old_links.find(path);
            if(old!=a.old_links.end()) p.validate_owned_symlink(path,old->second.target,old->second.witness);
            else {
                if(e.cause==ProjectionCause::OwnershipMismatch) throw;
                try { ExecutionAccess::absent(p,path,false); }
                catch(const ProjectionError& ancestor) {
                    if(ancestor.cause!=ProjectionCause::UnsafeAncestor ||
                        (ancestor.error!=ENOENT && !blocker(path))) throw;
                }
            }
            complete=false;
        }
    }
    for(const auto& path:a.desired_directories) {
        try { ExecutionAccess::directory(p,path,false); }
        catch(const ProjectionError& e) {
            if(e.error!=ENOENT && !blocker(path)) throw;
            complete=false;
        }
    }
    for(const auto& [path,link]:a.old_links) {
        ExecutionAccess::witness(p,path,link,false);
        if(a.desired_links.contains(path) || a.desired_directories.contains(path)) continue;
        try { ExecutionAccess::absent(p,path,false); }
        catch(const ExecutionError& e) {
            if(e.cause!=ExecutionCause::IncompleteBase) throw;
            p.validate_owned_symlink(path,link.target,link.witness); complete=false;
        }
    }
    ExecutionAccess::require_selected(p,a); p.revalidate();
    return {complete?ReconciliationState::Complete:ReconciliationState::Incomplete,ProjectionOutcome::NotMutated};
}
}
