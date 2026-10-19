/*
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 */
# Keep rules for the Java 25 probe. The probe is not part of the app; these
# rules only prevent R8 from removing the entry point while checking that R8
# accepts class-file version 69.
-dontshrink
-dontoptimize
-dontobfuscate
-keep class org.openi2pd.java25probe.Java25Probe { *; }
-keep class org.openi2pd.java25probe.Java25Probe$* { *; }
