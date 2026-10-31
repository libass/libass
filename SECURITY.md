# Reporting Security Issues

Thanks for taking a look at and testing libass!  
Below are some pointers helping with (further) testing and reporting.

## Supported Versions

Only the most recent release and the unreleased git HEAD state are considered supported.
Testing and analysis are strongly encouraged to be performed on one or both of these.

If you find something affecting an older but not the most recent release,
we most likely not consider this relevant.

If you find something affecting a git state after the most recent release
but no longer current git HEAD, then if the fix seems intentional this too
will no longer be relevant.  
If however, it seems like a mere coincidence with the exploit fixed unintentionally
by pure chance it might still be worth bringing to our attention.

## What qualifies as a security bug

Security bugs should be in the core libass library.
Our extra utilities like `test`, `compare`, etc are not built by default,
never installed and only intended for development and testing purposes.
Nasty bugs here still ought to be fixed but do not warrant special treatment.

The following are likely worth being tentatively treated as security relevant:

- memory out-of-bounds writes and reads or use-after-free
- arbitrary code execution
- arbitrary file system write or reads
- confidential information leakage to an attacker

On the other hand, the following are not security relevant for libass:

- memory leakage or other resource leakages like never-closed file descriptors
- resource exhaustion; e.g. attempting to allocate as much or more memory than the system can provide
- any other form of DOS
- signed-integer overflows _unless_ this overflow
  later leads to other exploitable issues like memory bugs
- bugs not directly in libass, but our dependencies,
  should be directly reported to the affected dependency instead

## How to test

The most convenient way to test libass behaviour on a test input file
is our `fuzz` utility compile in the default standalone mode.
You can enable it with `--enable-fuzz` during compilation.

If our bug relies on other API usages, like e.g. Matroska packets as inputs
instead of full files or obscure configurations, you may unfortunately need
to write a small utility yourself, though `fuzz`’ source may be
a useful starting point.

## Reporting

If after reading the above you believe you have found a bug with potential
security implications making it unfit to just be publicly reported as a
regular bug, you may use any of the following avenues:

- use GitHub’s “Report a vulnerability” feature on our repo’s “Security” tab
- ask on our IRC channel *(see `README.md`)* for
- contact a maintainer listed **both** in our `MAINTAINERS` file _and_
  [OSS-Fuzz’ contact list](https://github.com/google/oss-fuzz/blob/master/projects/libass/project.yaml)
  via a PGP-encrypted email

Regardless of how you choose to contact us,
please make sure to include details of what the bug is,
why you believe it to be security relevant and
how to trigger it with at least one reproducer test input.

If you wish to immediately also propose a patch with a fix,
make sure to check out the hints in our `CONTRIBUTING.md` file.
