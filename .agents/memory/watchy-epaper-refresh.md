---
name: Watchy e-paper refresh
description: Refresh strategy for the Watchy v2.0 GDEH0154D67 screen.
---

Use a full waveform refresh whenever the displayed preset changes. The fast partial waveform leaves visible residual glyphs on the physical Watchy; status-only changes can remain partial.

**Why:** A real-device photo showed severe remnants after preset changes made with the prior partial-waveform strategy. The D67 driver's full refresh is about 2.6 seconds, compared with about 0.5 seconds for partial refresh.

**How to apply:** Keep preset transitions on the full-refresh path and reserve partial refresh for minor status updates. Account for the slower display response when changing the input loop.