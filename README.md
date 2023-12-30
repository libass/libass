[![Build status](https://github.com/libass/libass/actions/workflows/ghci.yml/badge.svg?branch=master&event=push)](https://github.com/libass/libass/actions/workflows/ghci.yml?query=branch%3Amaster+event%3Apush)

[![Coverity scan build status](https://scan.coverity.com/projects/3531/badge.svg)](https://scan.coverity.com/projects/3531)

libass
======
libass is a portable subtitle renderer for the ASS/SSA (Advanced Substation Alpha/Substation Alpha) subtitle format. It is mostly compatible with VSFilter.

Get it
======
See [GitHub releases](https://github.com/libass/libass/releases) for the latest release 0.17.1 (released 2023-02-26).
See the [changelog](https://github.com/libass/libass/blob/master/Changelog) for a detailed list of changes.

Source code is available from our [GitHub repository](https://github.com/libass/libass).

Contact
=======
Please use the [issue tracker](https://github.com/libass/libass/issues?state=open) to report bugs or feature requests.

We have an IRC channel, too. Talk to us on [irc.libera.chat/#libass](https://web.libera.chat/#libass). Note that we cannot be online all the time and we cannot answer IRC questions if you leave the channel. Even if you do not get an immediate response, keep your IRC client open, and we will eventually get back to you.

Related Links
=============
The following projects/companies use libass:

- [MPlayer](http://www.mplayerhq.hu/)
- [mplayer2](http://www.mplayer2.org/)
- [mpv](http://mpv.io/)
- [VLC](http://www.videolan.org/)
- [GStreamer](http://gstreamer.freedesktop.org/) (assrender plugin)
- [FFmpeg](http://ffmpeg.org/)
  - [Libav](http://libav.org/) (unmaintained)
- [Aegisub](http://www.aegisub.org/)
- [Kodi (XBMC)](http://kodi.tv/)
- [avidemux](http://fixounet.free.fr/avidemux/)
- [PunkGraphicsStream (BD subtitle encoder)](http://code.google.com/p/punkgraphicstream/)
- [HandBrake](http://handbrake.fr/)
- [MX Player](https://play.google.com/store/apps/details?id=com.mxtech.videoplayer.ad)
- [QMPlay2](http://zaps166.sourceforge.net/?app=QMPlay2)
- [SubtitlesOctopus](https://github.com/libass/JavascriptSubtitlesOctopus) (JavaScript ASS via wasm libass)
  - [Crunchyroll](http://www.crunchyroll.com/) uses SubtitlesOctopus
- [AssRender for Avisynth(+)](http://avisynth.nl/index.php/AssRender)

Information about the ASS format:
=================================
- [ASS format overview](https://github.com/libass/libass/wiki/ASS-File-Format-Guide)
- [ASS override tags (Aegisub manual)](http://docs.aegisub.org/latest/ASS_Tags/)
- [VSFilter source code (Guliverkli2)](http://sourceforge.net/p/guliverkli2/code/HEAD/tree/src/subtitles/)

Other ASS/SSA implementations:
==============================
- VSFilter:
  - [xy-VSFilter/XySubFilter](https://github.com/Cyberbeing/xy-VSFilter/)
    - [pfmod](https://github.com/pinterf/xy-VSFilter/)
  - VSFilter in [MPC-HC](https://github.com/clsid2/mpc-hc/tree/develop/src/filters/transform/VSFilter/)
  - [VSFilterMod](https://code.google.com/archive/p/vsfiltermod/) (with custom format extensions)
    - [sorayuki fork](https://github.com/sorayuki/VSFilterMod/) with some bugfixes
  - [Threaded VSFilter](https://code.google.com/p/threaded-vsfilter/) (defunct)
  - VSFilter in [Guliverkli2](http://sourceforge.net/projects/guliverkli2/) (defunct, subsumed by all of the above)
  - VSFilter in [guliverkli](http://sourceforge.net/projects/guliverkli/) (defunct, forked as Guliverkli2)
- [ffdshow](http://ffdshow-tryout.sourceforge.net/) (defunct)
- [Perian](https://github.com/MaddTheSane/perian) (defunct)
- [asa](https://web.archive.org/web/20110906033709/http://asa.diac24.net/) (defunct)
- [libjass](https://github.com/Arnavion/libjass) (defunct)
- [ASS.js](https://github.com/weizhenye/ASS)
