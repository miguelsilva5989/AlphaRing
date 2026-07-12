# Third-Party Notices

AlphaRing includes or builds the following third-party software. Each component remains under its own license.

| Component | Source | License |
| --- | --- | --- |
| Dear ImGui | https://github.com/ocornut/imgui | MIT |
| MinHook | https://github.com/TsudaKageyu/minhook | 2-clause BSD |
| tinyxml2 | https://github.com/leethomason/tinyxml2 | zlib |
| spdlog | https://github.com/gabime/spdlog | MIT |
| JSON for Modern C++ | https://github.com/nlohmann/json | MIT |

Pinned source revisions for Dear ImGui, MinHook, and tinyxml2 are recorded as Git submodules. License files are retained in their source trees. spdlog and JSON for Modern C++ are currently vendored as headers under `lib/` with their license files.

AlphaRing also incorporates project-specific research and code inherited through the forks listed in [README.md](README.md#project-lineage-and-credits). The root [LICENCE.txt](LICENCE.txt) applies to AlphaRing's own code.
