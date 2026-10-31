# Contributing to libass

## General

If you’re not sure how to start, just getting an unpatched git HEAD
building as per our `README.md` and checking out the available
config options might be a good idea.

If your patch is quite large and/or requires significant changes to the internal
code organisation or interfaces or affects public API in any way, it is
**strongly** recommended to discuss the concept with us first.
This saves you time by not investing effort into drafting patches based on an
unviable approach, and is better for us too as we don’t have to review and
figure out how to salvage a large patch set based on a flawed concept.

Try to match the existing coding style of the individual file(s) you modify.

For discussion or to ask questions about the code base or your patch,
you can reach us via IRC *(see `README.md`)*.
For more lengthy or asynchronous discussions about patch concepts,
RFC issues or GitHub discussions can be used too.

## Code of Conduct

Aim to stay courteous and respectful.
We want to be welcoming space for all sorts of diverse participants.

Naturally, to remain a generally welcoming and friendly space
we must not be welcoming nor tolerant of anything or anyone
inherently detrimental to this. Thus bigotry, \*ism, \*phobia
and other such intolerance have no place here.

## Testing

Make sure your changes compile in all supported build systems.

Beyond that, we have a simple *(unfortunately rather incomplete)*
test suite at [libass-tests](https://github.com/libass/libass-tests).
To use it, clone this repository and during configure time enable the
`compare` and `fuzz` utilities and finally also indicate where the
cloned repo is located.

For autotools, this can be done with:

```
./configure --enable-compare --enable-fuzz ART_SAMPLES=../libass-tests
make -j "$(nproc)"
make check
```

For meson:

```
meson setup -Dcompare=enabled -Dfuzz=enabled -Dart-samples="$HOME"/code/libass-test build_meson
meson compile -C build_meson
meson test -C build_meson test
```

On some platforms certain tests relying on system libraries
might need to be skipped. You can use our CI setup for reference
and check documentation in the test repo for further info about
how to tweak the test suite.

## Commit guidelines

### Split changes into multiple commits

- Follow git good practices, and split independent changes into several commits.
  It's usually OK to put them into a single pull request.
- Try to separate cosmetic and functional changes.
- Splitting changes does _not_ mean that you should make them as fine-grained
  as possible. Commits should form logical steps in development. The way you
  split changes is important for code review and analysing bugs.

### Always squash fixup commits when making changes to pull requests

- If you make fixup commits to your pull request, you should generally squash
  them with `git rebase -i`, `git history fixup` or similar.
  We prefer to have pull requests in a merge-ready state suitable
  to be applied as a fast-forward to our development branch.

### Write good commit messages

- Write informative commit messages. Use present tense to describe the
  situation with the patch applied, and past tense for the situation before
  the change.
- subject lines are preferred to be in the style of `subcomponent: description`
  when the change is clearly limited or mainly related to one such subcomponent.
  For cosmetic only changes, an additional `cosmetic/` prefix can be applied.
  Check out the git log of the files you’re editing for reference.
- The body of the commit message (everything else after the subject line) must
  be as informative as possible and contain everything that isn't obvious. Don't
  hesitate to dump as much information as you can - it doesn't cost you
  anything. Put some effort into it. If someone finds a bug months or years
  later, and finds that it's caused by your commit (even though your commit was
  supposed to fix another bug), it would be bad if there wasn't enough
  information to test the original bug. The old bug might be reintroduced while
  fixing the new bug.

  The commit message must be wrapped on 72 characters per line, because git
  tools usually do not break text automatically. On the other hand, you do not
  need to break text that would be unnatural to break (like data for test cases,
  or long URLs).

### Signed commits

Signing your commits is all fine and good.
To preserve your signature before pushing to our development branch
we may need to ask you to rebase a patch series however.
If you do not rebase in a reasonable amount of time, we may need to do so
ourselves, unfortunately stripping the signature in the process.

## Copyright and licencing

- Make sure you own the copyright or have been granted permission by the
  original author(s) for any patches you send.
- If you’re not the original author of the content, make sure to say so and
  properly attribute the original author(s)!
- Any new submission must be licensed under the ISC licence matching the
  project-wide default, or a compatible equivalent or more permissive licence.
  If something is _not_ licensed under the ISC, make sure to explicitly
  highlight this!
- If a new copyright header is necessary (because a new file was created)
  a generic copyright statement like “libass contributors” is preferred.
  Naming each individual contributor in the header is not necessary nor helpful
  for copyright and licence purposes

## "AI"-assisted contributions

All contributions must have been in full thought up by yourself and other attributed authors.  
It is expressly forbidden to contribute any content whose essence has been created
with the assistance of Natural Language Processing artificial intelligence tools.
This also applies to how any parts re-used from other authors were authored.

### Examples

Usage of so called "generative AI" tools, "AI agents" or similar
to generate a patch in full or part is thus never permissible.

If you struggle with English to the degree of mostly relying on machine translation
*(nowadays typically also marketed as "AI")* for any meaningful natural-text communication
about libass and your patches, you are not forbidden from participating.
However, please refrain from any more changes to existing documentation
or other natural text parts then strictly necessary and do not add new natural text content.  
_Only_ use plain translations of your native remarks though!
Do **not** use any machine-generated prose extension or stylistic alterations.
It doesn’t help; on the contrary it will make it harder to figure out what you mean.

Using machine learning models for purely assistive purposes during the
creation process, e.g. as the voice synthesiser of a screen reader
or as a part of the voice-controlled input method, provided it
merely acts as a direct input method of the logic and code you
independently thought up, are permissible.

However, to reiterate, all logic, code and other essence of the contribution
must have always been independently thought up by yourself!

## Sending patches

Preferably send patches as a GitHub pull request.

If the need for a GitHub account is a hurdle to you,
you may also contact us on IRC and share a `format-patch`
series. In this case, you will be expected to stay around
on IRC for further discussion and revisions of the patches.
