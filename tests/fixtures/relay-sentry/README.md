# Relay sentry

This original JK/MotS fixture describes a stationary relay with a pivoting boom. The template binds its model, puppet, sound class and class COG. The model uses the indexed relay material, the puppet selects a short KEY, and the sound class plays a generated WAV. The two-geoset model and the single-geoset companion share the base and boom hierarchy.

Run `python generate.py` in this directory to reproduce the models, material and WAV. The template, KEY, PUP, SND and COG are authored text. None of the files contain retail art, audio or scripts.

The portable project tests generate candidate project and stock archives, then inspect the winning resources. They also remove the WAV to exercise a missing dependency and package a clean complete family for byte comparison.
