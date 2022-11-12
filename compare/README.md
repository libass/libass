Regression Testing
==================

To build a test utility configure libass with the `--enable-compare` flag.
The utility works with `png` image files so there is external dependency of libpng.

Test program command line:  
`compare ([-i] <input-dir>)+ [-o <output-dir>] [-s <scale:1-8>[x<scale:1-8>]] [-p <pass-level:0-3>]`

* `<input-dir>` is a test input directory, can be several of them;
* `<output-dir>` if present sets directory to store the rendering results;
* `<scale>` sets factors for horizontal and vertical oversampling (positive integers up to 8)
  or a single factor to be used in both directions (default 1);
* `<pass-level>` corresponds to the level of image differences required to pass test:
  - 0: only `SAME` level accepted, bitwise comparison mode;
  - 1: `GOOD` level or less required;
  - 2: `BAD` level or less required, default mode;
  - 3: `FAIL` level or less required, i. e. any difference accepted, error checking mode.

An input directory consists of font files (`*.ttf`, `*.otf` and `*.pfb`), subtitle files (`*.ass`), and image files (`*.png`).
All the fonts required for rendering should be present in the input directories as
libass is configured to not use anything external outside of it (`ASS_FONTPROVIDER_NONE`).
After loading all the fonts in the directories, the test program scans for subtitle files (`<sub_name>.ass`)
and their corresponding image files in the form of `<sub_name>-NNNN.png`,
where `NNNN` is an arbitrary number of decimal digits.
The subtitle file then would be rendered at the time of `NNNN` milliseconds and compared with the loaded image file.
For example, an input directory can have the following structure:

```
test/
  font1.ttf
  font2.otf
  sub1.ass
  sub1-0500.png
  sub1-1500.png
  sub1-2500.png
  sub2.ass
  sub2-153000.png
```

More precisely, the test program (`compare`) would scan all input directories and do the following:
1) load all the fonts found (`*.ttf`, `*.otf`, `*.pfb`);
2) search for subtitle files (`*.ass`);
3) for every file found (`<sub_name>.ass`) scan for the files with names of `<sub_name>-NNNN.png`;
4) interpret `NNNN` as a time in milliseconds and render the subtitle file at that time with a transparent background;
5) fuzzy compare the rendering result with the corresponding png file;
6) save the rendering result in the output directory if requested.

Target images should be transparent RGBA png files with 8 or 16 bits per channel.
A subtitle rendering resolution is determined by the extents of the target image files multiplied by an optional scale factor (`-s` switch).
In the case of nontrivial scale factor, the result would be downsampled using 16-bit precision before comparison.
Downsampling is done assuming a linear color space which is physically incorrect but commonly used in computer rendering.

It's possible to save rendering results by using `-o` switch.
Saved files would have the same names as the target image files.
That functionality can be used for an initial generation of target images
by supplying arbitrary source images with correct names and extents.

Test program output can look like the following:
```
Loading font 'font1.ttf'.
Loading font 'font2.otf'.
Processing 'sub1.ass':
  Time 0:00:00.500 - 2.464 BAD
  Time 0:00:01.500 - 1.412 OK
  Time 0:00:02.500 - 4.919 FAIL
Processing 'sub2.ass':
  Time 0:02:33.000 - 0.728 OK
Only 3 of 4 images have passed test
```
For each target image file the program reports a maximal ratio of the per pixel comparison difference to the baseline error scale.
The baseline error scale tries to take account of neighboring pixels to estimate visibility of a difference in that specific pixel location.
Zero difference is marked as `SAME` (level 0), nonzero value in the range of [0–2.0] is marked as `GOOD` (level 1),
in the range of [2.0–4.0] as `BAD` (level 2) and more than 4.0 as `FAIL` (level 3).
Any problem during processing of a specific image is treated as a level 4 difference.
If all images have level of difference less or equal to pass level (`-p` switch, default 2) then the test is considered as passed
and program returns 0. Otherwise program returns maximal level of difference throughout all images (4 in case of error).

Note that almost any type of a rendering error can be greatly exaggerated by the specially tailored test cases.
Therefore test cases should be chosen to represent generic real world scenarios only.
