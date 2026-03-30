# Issue #16 – Phase 3 Adopt-In-Place Decision Memo

## Decision Framing

Phase 2 already delivers a safe imported-project workflow:

- scan an existing REAPER project
- review likely WING channel matches
- create a separate managed layer
- leave the imported tracks untouched

Phase 3 would change the contract:

- WINGuard would adopt the imported tracks themselves
- routing and managed metadata would be written onto those existing tracks

That makes Phase 3 useful, but materially riskier.

## Reasons To Stop At Phase 2

Stop at Phase 2 if the main user need is:

- safely evaluating whether an imported project can be used for soundcheck
- getting WING-managed tracks quickly without risking the original session
- keeping the imported project intact for comparison or fallback

Phase 2 is likely enough when:

- duplicate tracks are acceptable
- users prefer a non-destructive workflow
- imported projects are often messy, inconsistent, or shared by other engineers
- the team values safety more than convenience

Benefits of stopping at Phase 2:

- lowest risk of damaging a project
- easy mental model
- no need to solve rollback or destructive-edit UX yet
- easier cross-platform support

## Reasons Phase 3 Might Still Be Worthwhile

Phase 3 makes sense only if users strongly prefer to keep working on the imported tracks directly.

Good reasons to proceed:

- users consistently dislike the duplicate managed layer
- imported tracks already contain the exact folders, FX chains, colors, and naming they want to keep
- operators want the original tracks themselves to become the managed soundcheck tracks
- the extra managed layer creates enough clutter to slow real workflows

## Risks Unique To Phase 3

Phase 3 introduces risks that Phase 2 intentionally avoids:

- a wrong mapping changes the real project, not a parallel managed layer
- stereo mistakes become more expensive
- stems, prints, buses, click tracks, or utility tracks can be adopted accidentally
- user trust drops quickly if WINGuard rewrites imported tracks in a surprising way

## Conditions Required Before Phase 3

Do not start Phase 3 unless these conditions are accepted as design constraints:

1. `Adopt In Place` stays optional and never becomes the default.
2. `Create Managed Layer` remains the safer primary path.
3. Adoption is driven by explicit per-track confirmation.
4. The workflow preserves by default:
   - track names
   - track order
   - folder structure
   - FX chains
   - media items
   - colors
5. In v1, Phase 3 rewrites only:
   - managed source metadata
   - confirmed input routing
   - confirmed hardware output routing
6. Channels-only scope remains in place at first.
7. Cancel/close before final confirmation performs no writes.
8. The confirmation screen explicitly lists every track that will be changed.

## Recommended Gate For Proceeding

Only proceed to Phase 3 after manual testing shows that:

- Phase 2 is functionally correct
- users understand the adoption review flow
- users repeatedly ask to avoid the extra managed layer

If that evidence is not there, Phase 2 is the better stopping point.

## Recommendation

Current recommendation:

- keep Phase 2 as the default imported-project workflow
- treat Phase 3 as conditional follow-up work, not an assumed next step
- only implement `Adopt In Place` if real user testing shows the duplicate managed layer is a meaningful workflow problem
