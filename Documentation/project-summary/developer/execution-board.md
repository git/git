# Execution Board

## Purpose

This document turns `q2-2026-plan.md` into a lightweight execution board for the current planning cycle.

## Status Meanings

- `pending`: important but not started
- `in progress`: actively being worked
- `blocked`: cannot move without another decision or dependency
- `done`: materially completed for this cycle

## Goal 1: Strengthen `git ace`

- `done` Implement `git ace status` for stack-wide status and health reporting.
- `done` Define how metadata drift should be detected and explained to users.
- `done` Implement Ace Sequencer for conflict UX in `rebase-stack` and `merge-stack`.
- `pending` Expand documentation around rename/delete/parent edge cases.
- `pending` Identify the highest-risk test gaps for Ace metadata operations.

## Goal 2: Clarify Product And Organization Identity

- `done` Establish project-summary overview docs.
- `done` Establish organization profile and open-community operating model.
- `done` Define governance summary.
- `done` Define release criteria and Ace 1.0 release policy.
- `pending` Keep the summary current as implementation work changes the product.

## Goal 3: Establish Delivery Discipline

- `done` Create quarterly planning template.
- `done` Create a filled-in quarter plan.
- `done` Add roles and owners.
- `pending` Translate roadmap items into implementation-tracked work.
- `pending` Define stronger testing strategy for highest-risk commands.

## Near-Term Candidate Implementation Tasks

- `done` Add explicit tests for Ace branch rename with descendants.
- `done` Add explicit tests for Ace branch deletion fallback behavior.
- `done` Add tests for cycle prevention in parent assignment.
- `done` Add documentation and test strategy for agent environment contract.
- `done` Add preview/safety design notes for `git history` and `git replay`.

## Known Constraints

- leadership bandwidth is concentrated in the co-leads
- ambitious roadmap items can outpace implementation and testing capacity
- documentation quality is currently ahead of some implementation maturity, which is useful but must stay honest

## Update Rule

When a material roadmap or implementation step changes, update this board so it remains a useful coordination artifact rather than a frozen wish list.
