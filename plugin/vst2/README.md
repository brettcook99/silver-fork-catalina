# VST2 interface headers (FST)

JUCE builds the VST2 ("VST") version of the plugin from the headers
`pluginterfaces/vst2.x/aeffect.h` and `aeffectx.h`. Steinberg's VST2 SDK is
no longer available, so these are the headers of **FST** ("Free Studio
Technology"), a reverse-engineered VST2-compatible interface by IOhannes m
zmölnig (IEM): https://git.iem.at/zmoelnig/FST, FST 0.177.0, commit
647af068765b75867e3a28b4dd8991ab9ed47f7c. They are unchanged.

FST is licensed under the GNU General Public License, version 3 or later
(`LICENSE`). GoatTracker is distributed under the GNU General Public License
without a version given, so the plugin built with FST is under GPL version 3
or later.
