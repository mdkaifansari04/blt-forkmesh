# ForkMesh World follow-up

- [ ] Render every live mirror as its own user-height server cabinet with status lights.
- [ ] Show each mirror's real Git hash, CPU, memory, disk, repository, health, sync, integrity, and repository statistics on its cabinet; show unavailable states instead of invented values.
- [ ] Replace pointer lock/always-on mouse look with visible-cursor click-and-drag camera rotation.
- [ ] Remove click-to-follow movement while preserving keyboard, touch, and accessible navigation.
- [ ] Accelerate player movement gradually while a movement key is held, with a safe capped speed and immediate reset on release.
- [ ] Keep world lighting spatially stable and add a local user-controlled light-level setting.
- [ ] Restore the user's last world position and heading on refresh without publishing precise history.
- [ ] Make full-page refreshes fetch current World assets on mobile and desktop instead of reusing stale cached application code.
- [ ] Reduce movement/presence socket traffic, coalesce updates, and make busy clients reconnect without activity-driven disconnects.
- [ ] Show real pull-request counts and let users open a pull request inside the repository world.
- [ ] Add an in-world pull-request review panel with a file tree, compact code diff viewer, and per-file viewed state as each diff is scrolled.
- [ ] Let an authorized repository owner merge an eligible pull request into the main branch from the review panel, with server-side authorization and branch checks.
- [ ] Add opt-in mirror-node Actions capability and advertise its real enabled/disabled/running state.
- [ ] Let the Qt controller configure mirror Actions and local deployment variables without sending secrets into public World state or logs.
- [ ] Run enabled Actions on the selected mirror node and return bounded, redacted status/log summaries to authorized users.
- [ ] Validate, deploy, and post a global World notification after every completed checklist feature.
