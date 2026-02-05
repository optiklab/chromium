# URL Pattern Rust FFI - Crate Addition Guide

## Overview

This document provides step-by-step instructions for adding the `urlpattern` crate (v0.3.0)
to Chromium's third-party Rust dependencies.

## Prerequisites

- Chromium checkout with `depot_tools` in PATH
- Permission to create CLs (Googlers or contributors with access)
- Access to run `gnrt` tool

## Step-by-Step Instructions

### 1. Add Crate to Cargo.toml

Edit `third_party/rust/chromium_crates_io/Cargo.toml`:

```toml
[dependencies]
# ... existing dependencies ...
urlpattern = "0.3.0"
```

### 2. Run gnrt vendor

This downloads the crate and all its dependencies:

```bash
$ cd /path/to/chromium/src
$ tools/crates/run_gnrt.py vendor
```

**Expected output:**
```
Downloading crates ...
  Downloaded urlpattern v0.3.0
  Downloaded regex v1.x.x (transitive dependency)
  ... (other transitive dependencies)
Vendoring complete.
```

**Result:**
- Crates downloaded to `third_party/rust/chromium_crates_io/vendor/`
- `urlpattern-v0_3/` directory created

### 3. Run gnrt gen

This generates BUILD.gn files for all crates:

```bash
$ tools/crates/run_gnrt.py gen
```

**Expected output:**
```
Generating BUILD.gn files ...
  Generated third_party/rust/urlpattern/v0_3/BUILD.gn
  ... (other BUILD.gn files for dependencies)
Generation complete.
```

### 4. Configure the Crate

Edit `third_party/rust/chromium_crates_io/gnrt_config.toml`:

```toml
[crate.urlpattern]
# Security group - will be set to 'safe' after security review
# Start with 'sandbox' to be conservative
group = 'sandbox'

# Allow use in first-party Chromium code
allow_first_party_usage = true

# Mark as security critical (requires review)
security_critical = true

# Mark as shipped (will be in production)
shipped = true

[crate.urlpattern.extra_kv]
# urlpattern uses some unsafe code internally for performance
# This is audited by security review
allow_unsafe = true

# Additional dependencies might need configuration too
# Check the gnrt output for warnings about other crates
```

### 5. Test the Build

Verify the crate can be built:

```bash
$ gn gen out/Default
$ autoninja -C out/Default third_party/rust/urlpattern/v0_3:lib
```

**Expected**: Successful build with no errors.

### 6. Create Security Review CL

Create a CL for security review:

```bash
$ git checkout -b add-urlpattern-crate
$ git add third_party/rust/chromium_crates_io/Cargo.toml
$ git add third_party/rust/chromium_crates_io/Cargo.lock
$ git add third_party/rust/chromium_crates_io/vendor/
$ git add third_party/rust/urlpattern/
$ git add third_party/rust/chromium_crates_io/gnrt_config.toml
$ git commit -m "Add urlpattern v0.3.0 crate

Adding the urlpattern crate for replacing the C++ liburlpattern
implementation to address memory safety issues.

Crate details:
- Version: 0.3.0
- License: MIT (compatible)
- Maintainer: Deno team
- Purpose: URL pattern matching per URLPattern spec

Security review required per //third_party/rust/OWNERS-review-checklist.md

Bug: XXXXXX
"
$ git cl upload
```

### 7. Security Review Process

1. **Add Reviewers**: chrome-third-party-rust-reviews@google.com
2. **Notify Team**: Send email to chrome-atls-discuss@google.com with:
   ```
   Subject: New Rust crate: urlpattern v0.3.0
   
   Adding urlpattern crate for replacing liburlpattern C++ implementation.
   
   Crate: urlpattern
   Version: 0.3.0
   License: MIT
   Repository: https://github.com/denoland/rust-urlpattern
   Purpose: URL pattern matching (URLPattern web API)
   
   CL: https://chromium-review.googlesource.com/c/chromium/src/+/XXXXXX
   
   Security review in progress.
   ```

3. **Review Checklist** (from `//third_party/rust/OWNERS-review-checklist.md`):
   - ✅ Crate license is compatible (MIT)
   - ✅ Crate is from a reputable source (Deno team)
   - ✅ Crate has active maintenance
   - ✅ No known security vulnerabilities
   - ✅ Use of `unsafe` is justified and audited
   - ✅ Dependencies are acceptable
   - ✅ Crate is necessary (replaces unsafe C++ code)

### 8. Land the CL

After LGTM from security reviewers:

```bash
$ git cl land
```

### 9. Update gnrt_config.toml (Post-Review)

After security review approves, you may update:

```toml
[crate.urlpattern]
group = 'safe'  # Upgraded from 'sandbox' after security review
# ... rest unchanged ...
```

## Troubleshooting

### Issue: License File Not Found

If `gnrt` warns about missing license files:

1. Check `third_party/rust/chromium_crates_io/vendor/urlpattern-v0_3/`
2. Look for LICENSE, LICENSE.md, or similar
3. If needed, add to `gnrt_config.toml`:
   ```toml
   [crate.urlpattern]
   license_files = ["LICENSE"]
   ```

### Issue: Build Failures

If the crate doesn't build:

1. Check for platform-specific issues
2. May need to disable certain features:
   ```toml
   [crate.urlpattern]
   features = []  # Disable default features if needed
   ```

### Issue: Transitive Dependencies

If transitive dependencies cause issues:

1. Check `gnrt` output for warnings
2. May need to configure each transitive dependency
3. Consider using `cargo tree` to understand dependency graph

### Issue: Test-Only Dependency

If a crate is only needed for tests:

```toml
[crate.some_test_dep]
group = 'test'  # Reduces security review requirements
```

## Verification

After landing, verify the crate is available:

```bash
$ gn desc out/Default //third_party/rust/urlpattern/v0_3:lib
```

Should show the build target with proper dependencies.

## Next Steps

Once the crate is added, proceed with:
1. Implementing the FFI bridge (see `IMPLEMENTATION_PLAN.md`)
2. Integrating with liburlpattern C++ wrapper
3. Running tests

## References

- Chromium Rust Guide: `//docs/rust.md`
- Adding Crates: `//tools/crates/create_update_cl.md`
- Security Review: `//third_party/rust/OWNERS-review-checklist.md`
- gnrt Tool: `//tools/crates/gnrt/`
