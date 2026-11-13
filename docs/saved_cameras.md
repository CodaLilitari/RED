# Camera poses and saved viewpoints

**View > Camera Pose and Saved Viewpoints** edits the current viewport numerically
and retains named viewpoints in the editor workspace. These viewpoints do not
create game cameras, move player spawns, modify the Level, change selection, or
add Level Undo records.

Use **Capture current** to read the current camera into the form. **Apply form**
navigates to its accepted pose. Selecting a saved name loads that pose into the
form; **Recall selected** navigates to the stored pose. Typing and selecting do
not move the viewport. Closing discards unapplied form edits and retains any
navigation already applied.

**Save new** stores the form under its name without navigating. **Replace pose**
updates the selected name's pose, **Rename** changes its name, and **Delete**
removes it. Names are trimmed and repeated whitespace collapses to one space.
Names must be unique ignoring case, contain well-formed Unicode without control
characters, and fit within 128 UTF-16 code units. A workspace retains at most 256
viewpoints. Failed validation or an owner refusing publication leaves the list
unchanged. A dialog whose document or viewport context has changed refuses later
navigation and list updates; close and reopen it for the new context.

The form scrolls while its actions stay visible on smaller displays. Numeric
controls expose eye XYZ, orbit pitch/yaw/roll, distance and bound radius,
projection, horizontal FOV, orthographic scale, clipping, near, and far.

## Coordinates and restore limits

XYZ denotes the camera eye in RED's native map units: X right, Y forward, Z up.
No metres conversion is implied. Angles are the editor's orbit degrees, not an
interchangeable engine look-pitch tuple. Yaw zero looks along +Y, positive 90
degrees looks along -X, and positive orbit pitch looks downward. Roll banks the
right/up basis around the view direction. Changing eye/angles/distance derives
the orbit target from that eye and direction; a form without those changes
retains its captured centre/pan representation.

Poses retain the captured logical viewport size and device-pixel ratio (DPR).
Orthographic recall preserves **horizontal world units per logical pixel**:

```text
units = roundedPhysicalHeight / (storedScale * storedLogicalWidth)
newScale = newRoundedPhysicalHeight / (units * newLogicalWidth)
```

The renderer rounds physical width and height independently. At fractional DPR,
their ratio can differ slightly from the logical aspect ratio, so one scalar
cannot preserve both horizontal and vertical logical-pixel offsets exactly.
The vertical difference follows that framebuffer rounding. Derived extents use
the actual rounded framebuffer, and tests compare the real camera projection.

Admission requires finite bounded coordinates, a distinct eye and target at
the viewport's float precision, distance at least bound radius / 20, orbit pitch
in [-180, 180), FOV 1..179 degrees, and orthographic scale 0.01..1,000,000.
Perspective near must be at least 0.001; orthographic near must be at most zero;
far must exceed near and be at least near + 0.01. Coordinates, distances and
clipping planes are bounded to an absolute value of 1,000,000,000; yaw/roll are
bounded to 1,000,000 degrees. Logical dimensions are 1..65,535 and DPR is (0, 16].

Restore constructs and checks a candidate camera first. Invalid destination
dimensions, scale overflow, setter clamping, or nonfinite matrices produce a
diagnostic without publishing the candidate. Inactive projection clipping
settings remain intact. Capturing a pose reads the actual current camera; it
does not settle an animation or substitute an animation's future endpoint.
Native viewport float precision still applies.

## Workspace persistence and ownership

Named viewpoints belong to the optional EDIT root `savedCameras` map. The map
has integer `version: 1` and a `cameras` array. Each entry contains a normalized
`name` string and the 20-field `pose` array:

```text
[centreX, centreY, centreZ, panX, panY, panZ, radius, distance,
 yaw, pitch, roll, projection, fovX, orthoScale, near, far,
 clipping, logicalWidth, logicalHeight, DPR]
```

Projection is integer 0 for Perspective or 1 for Orthographic; clipping is a
Boolean and logical dimensions are integers. Other numeric fields must be
finite and satisfy pose admission. Both encoding and decoding enforce complete
list validation and its count/name limits. A malformed present `savedCameras`
map rejects the whole EDIT workspace before live mutation; an absent map means
an empty list. The addition retains EDIT V1 and all native SED container versions.

Manual native Save, autosave and recovery use the same workspace codec. A new
document or an engine/legacy Open starts with an empty named list; accepted
native EDIT restores its list. Native Save does not clear the current list.
Named-list updates request an editor workspace checkpoint without marking
Level content dirty. Runtime JKL/NDY and legacy editor exports do not carry this
list. See [native SED](sed_format.md) for optional workspace recovery and
[the user guide](user_guide.md) for ordinary viewport controls.

The app owns the pose types, Qt codec, dialog, and viewport adapter. The dialog
uses capture/apply/store/current-context callbacks and never owns a Level.
The reusable value-camera restore helper performs destination admission and
candidate construction before assignment. Image reports, performance timing,
and bulk camera image export are outside this feature.
