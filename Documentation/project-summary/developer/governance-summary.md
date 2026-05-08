# Governance Summary

## Governing Idea

AcreetionOS presents itself publicly as an independent, community-focused organization. In this repository, that should translate into governance that is transparent, technically grounded, and explicit about who decides what.

## Current Leadership

Current co-lead developers:

- `Darren Clift`
- `Natalie Cole-Clift Spiva`

## Decision-Making Model

For this repository, the most practical governance model is:

- co-leads set strategic direction
- technical changes are evaluated against Git compatibility, Ace product goals, and repository sovereignty
- community input matters most on user-facing workflow pain, documentation clarity, and roadmap priorities
- experimental features should remain clearly identified until they earn stronger guarantees

## Repository Sovereignty

This is not just branding language. It should affect design decisions.

In practice, it means:

- prefer inspectable local state over opaque hosted-only workflows
- preserve user and operator control over repository behavior
- do not make Ace depend conceptually on one centralized platform to be usable

## Governance Questions For Major Changes

Before large work lands, the project should be able to answer:

- who is the user for this change
- what problem is it solving
- what compatibility risk does it create
- what operational or security risk does it create
- what docs and tests need to change
- whether this is experimental, evolving, or stable

## Relationship To The Community

An open community organization still needs clear maintainership. Openness should not mean ambiguity about who owns decisions.

The healthy balance is:

- contributors can propose broadly
- maintainers decide clearly
- docs explain the project honestly

## Bottom Line

Governance in Ace should protect four things above all:

- independence
- transparency
- stability
- user and operator control
