# Tinmans Tuning Guide Help Links

Tinman setting labels, built-in tips, preference help, import/project help, and calibration
guides open https://github.com/Tinman-FP/tinmans-tuning-guide/wiki.
Links resolve to the relevant maintained chapter and setting group.
Manufacturer hardware/service and HMS links retain their model-specific targets.

The route contract lives in the tuning guide repository at `wiki/routes.json`.
Its wiki generator validates destination headings and emits
`src/libslic3r/TuningGuide.hpp`. The header records the contract SHA-256 and
requires no runtime download or writable user configuration. Unknown future
settings resolve to the Tinman settings reference, without transmitting the
unrecognized identifier or any private data in a URL.

Exact routes override group routes. For example, flow ratio and pressure
advance share an upstream wiki category but resolve to different guide
chapters. Both legacy short identifiers and newer folder-prefixed Orca wiki
identifiers resolve through the same helper.

To change a destination, edit the guide's contract, validate and publish its
wiki, regenerate this header using the guide's `scripts/build_wiki.py`, then
run `python3 -m unittest checks.test_tuning_guide_links`. Keep the wiki live
before distributing a binary that depends on its pages.

This update changes documentation destinations and product revision only.
Slicing algorithms, calibration generators, profile values, printer addresses,
credentials, and user profile storage are unchanged.
