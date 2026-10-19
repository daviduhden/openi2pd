# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: keep.pro.
#
# R8 keep rules for the standalone Java 25 class-file probe; the probe is not
# part of the application.
#

# Keep rules for the Java 25 probe. The probe is not part of the app; these
# rules only prevent R8 from removing the entry point while checking that R8
# accepts class-file version 69.
-dontshrink
-dontoptimize
-dontobfuscate
-keep class org.openi2pd.java25probe.Java25Probe { *; }
-keep class org.openi2pd.java25probe.Java25Probe$* { *; }
