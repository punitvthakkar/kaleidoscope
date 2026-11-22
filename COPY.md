# fractonaut 2026 — copy deck

Every word a person can see or hear in the app (the web app, the Mac app and the Android app), in one place.

## How to edit

*   **Edit only the "Text" column.** Leave the ID column alone: it is how your words get put back into the app.
*   **Keep anything in {braces}.** These are blanks the app fills in, such as {place}, {look} or {seconds}. You can move a blank within the line or drop it, but don't rename it.
*   **Use the "Notes" column** for anything you want to tell the engineer, such as "make this sound warmer" or "same as the line above".
*   **To remove a line**, write `(remove)` in the Text column.
*   **Length.** Some places have very little room:
    *   Ring labels (the small words under each circle on the ring): about 1 word, at most 2 short words.
    *   Dial ends (the two words under a slider): 1 word each.
    *   Centre lines (the line in the middle of the ring): one short line, about 6 words.
    *   Toasts (the short message that slides up from the bottom): one short line. The words after a `·` are a second thought, so keep them short too.
    *   Story lines in the Visit: one sentence each. Each line is on screen for about 7 seconds.
*   A `·` (middle dot) is how the app separates two short thoughts. Keep it or swap it for a full stop, whichever you like.
*   "Screen reader" in the "Where" column means the words are read aloud to blind and low-vision visitors and are never shown on screen. They should say the same thing as what sighted visitors see.
*   Spelling: the ring says **Colour** (British) while All settings says **Color** (American). Choose one and the engineer will make it the same everywhere.

* * *

## 1\. Brand & first impression

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `brand.page.title` | The browser tab, and the name a bookmark or home-screen icon gets | names the app and sets the mood | Fractonaut 2026. Come visit, O wanderer |   |
| `brand.wordmark.word` | The big logo at the centre of the ring. The "o" is drawn as the orb | the logo itself | fractonaut | The "o" is a picture of the orb, so the word must keep an "o" in it. |
| `brand.wordmark.year` | Small number beside the logo | the edition | 2026 |   |
| `brand.welcome.prefix` | Big caption when the app first opens, before the logo | the first greeting | Here you are | Followed by the logo. Read aloud together as "Welcome, fractonaut". |
| `brand.welcome.tagline` | Smaller line under the welcome caption | the tagline | How are you, fractonaut? |   |
| `brand.home.say` | Centre of the main ring, under the logo, when nothing is hovered | the resting tagline | An infinite adventure |   |

## 2\. The orb & the main ring

The orb is the round button at the bottom of the screen. Tap it and the main ring opens around it: twelve circles, each with a word under it (the label). When you point at a circle (or tap it), a line in the centre explains it (the "say" line).

### 2a. Main ring (clockwise from the top)

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `halo.home.place.label` | Main ring, circle 1 (shows a picture of the current place) | opens the places | Room | Ring label: 1 word |
| `halo.home.place.say` | Centre of the main ring when you point at Place | says what Place is | Your place |   |
| `halo.home.colour.label` | Main ring, circle 2 (shows the current colours) | opens the colours | Color | Ring label: 1 word |
| `halo.home.colour.say` | Centre of the main ring when you point at Colour | invites you to pick the light | Choose your light | Compare `colour.core.line`, which is nearly the same |
| `halo.home.hands.label` | Main ring, circle 3 | opens hand looks | Hands | Ring label: 1 word |
| `halo.home.hands.say` | Centre of the main ring when you point at Hands | says what Hands is | Your avatar in this universe |   |
| `halo.home.calm.label` | Main ring, circle 4 | opens the calm moods | Calm | Ring label: 1 word |
| `halo.home.calm.say` | Centre of the main ring when you point at Calm | says what Calm is | The gentleness of the journey |   |
| `halo.home.breathe.label` | Main ring, circle 5 (an on/off switch) | turns on the breathing guide | Breathe | Ring label: 1 word |
| `halo.home.breathe.say` | Centre of the main ring when you point at Breathe | explains how to breathe with the orb | Let yourself just be |   |
| `halo.home.listen.label` | Main ring, circle 6 (on/off) | lets music move the picture | Listen | Ring label: 1 word |
| `halo.home.listen.say` | Centre of the main ring when you point at Listen | explains Listen | Let your music into this universe |   |
| `halo.home.drift.label` | Main ring, circle 7 (on/off) | lets the picture wander on its own | Autopilot | Ring label: 1 word |
| `halo.home.drift.say` | Centre of the main ring when you point at Drift | explains Drift | Just observe the experience |   |
| `halo.home.keep.label` | Main ring, circle 8 | opens Keep (save a picture, film, link) | Remember | Ring label: 1 word |
| `halo.home.keep.say` | Centre of the main ring when you point at Keep | explains Keep | Capture your journey to share | Same words as `keep.core.line` |
| `halo.home.visit.label` | Main ring, circle 9 (on/off) | starts or stops the palace story | Palace | Ring label: 1 word |
| `halo.home.visit.say` | Centre of the main ring when you point at Visit | invites you into the story | Walking the palace, one question at a time |   |
| `halo.home.shelf.label` | Main ring, circle 10 | opens the shelf of bottles | Shelf | Ring label: 1 word |
| `halo.home.shelf.say` | Centre of the main ring when you point at Shelf | explains the shelf | Your words for your soul |   |
| `halo.home.light.label` | Main ring, circle 11 | opens Light (what paints) | Light | Ring label: 1 word |
| `halo.home.light.say` | Centre of the main ring when you point at Light | explains Light | The brush on the canvas |   |
| `halo.home.settings.label` | Main ring, circle 12 | opens All settings | Settings | Ring label: 1 word |
| `halo.home.settings.say` | Centre of the main ring when you point at Settings | explains Settings | Customize to your heart's content |   |

### 2b. Side buttons beside the orb (sound)

The hands button beside the orb is in section 5.

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `side.sound.on` | Tooltip on the speaker button beside the orb, while sound plays (also read by screen readers) | says sound is on | Sound is on | Short: tooltip |
| `side.sound.off` | Tooltip on the speaker button beside the orb, while it is quiet (also read by screen readers) | offers sound | Turn sound on | Short: tooltip |

## 3\. Places

### 3a. The Place ring and its centre

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `place.ring.all` | Last circle on the Place ring | opens every place at once, as a wall of pictures | All places | Ring label: 1–2 words |
| `place.core.turn` | Small button in the centre of the Place ring | rearranges the current place, like turning a real kaleidoscope | Turn the kaleidoscope | Small button |
| `place.wall.title` | Heading of the full-screen wall of places | asks where to go | Where to? |   |
| `place.wall.close` | Button at the top of the wall of places | closes the wall | Close | Small button |
| `place.wall.group.sky` | Section heading on the wall of places | groups the sky places | The sky |   |
| `place.wall.group.mirrors` | Section heading on the wall of places | groups the mirror places | Mirrors |   |
| `place.wall.group.bent` | Section heading on the wall of places | groups the bent-space places | Bent space |   |
| `place.wall.group.more` | Section heading on the wall of places (only if a place has no group) | catch-all | More | Not normally seen |

### 3b. Place names and their lines

Each place's name and line appear in three spots: the caption that fades in when you arrive, the centre of the Place ring, and the wall of places. The name is also the label under its circle on the Place ring.

The six places marked *(apps only)* exist only in the Mac and Android apps. The web version leaves them out.

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `place.mandala.name` | Place name (caption, ring label, wall) | names the place | Mandala | Ring label: 1–2 words |
| `place.mandala.line` | Line under the place name | says what the mirrors do here | Meet the mirrors |   |
| `place.tunnel.name` | Place name | names the place | Tunnel |   |
| `place.tunnel.line` | Line under the place name | describes it | Find the light at the end |   |
| `place.fractal.name` | Place name | names the place | Fractal |   |
| `place.fractal.line` | Line under the place name | describes it | Fold your conscience |   |
| `place.circle-limit.name` | Place name | names the place | Circle limit |   |
| `place.circle-limit.line` | Line under the place name | describes it | Approach the edge |   |
| `place.spiral.name` | Place name | names the place | Spiral |   |
| `place.spiral.line` | Line under the place name | describes it | Contain yourself within |   |
| `place.liquid.name` | Place name | names the place | Liquid |   |
| `place.liquid.line` | Line under the place name | describes it | Melt away into the visions |   |
| `place.crystal.name` | Place name | names the place | Crystal |   |
| `place.crystal.line` | Line under the place name | describes it | Drift in peace |   |
| `place.maze.name` | Place name | names the place | Maze |   |
| `place.maze.line` | Line under the place name | describes it | Your own path |   |
| `place.dissolve.name` | Place name | names the place | Dissolve |   |
| `place.dissolve.line` | Line under the place name | describes it | Disappear into nothingness |   |
| `place.dream.name` | Place name | names the place | Dream |   |
| `place.dream.line` | Line under the place name | describes it | Lull yourself into joy |   |
| `place.black-hole.name` | Place name | names the place | Black hole |   |
| `place.black-hole.line` | Line under the place name | describes it | Bend your light |   |
| `place.glass-ball.name` | Place name | names the place | Glass ball |   |
| `place.glass-ball.line` | Line under the place name | describes it | Reflect on reality |   |
| `place.pearls.name` | Place name | names the place | Many worlds |   |
| `place.pearls.line` | Line under the place name | describes it | Explore the multiverse |   |
| `place.poles.name` | Place name | names the place | Poles |   |
| `place.poles.line` | Line under the place name | describes it | Play with your vision |   |
| `place.galaxy.name` | Place name *(apps only)* | names the place | Galaxy |   |
| `place.galaxy.line` | Line under the place name *(apps only)* | describes it | Go far out |   |
| `place.nebula.name` | Place name *(apps only)* | names the place | Nebula |   |
| `place.nebula.line` | Line under the place name *(apps only)* | describes it | See time itself |   |
| `place.pulsar.name` | Place name *(apps only)* | names the place | Pulsar |   |
| `place.pulsar.line` | Line under the place name *(apps only)* | describes it | A dead star but still alive |   |
| `place.quasar.name` | Place name *(apps only)* | names the place | Quasar |   |
| `place.quasar.line` | Line under the place name *(apps only)* | describes it | Light is everything |   |
| `place.supernova.name` | Place name *(apps only)* | names the place | Supernova |   |
| `place.supernova.line` | Line under the place name *(apps only)* | describes it and teaches the gesture | The energy within |   |
| `place.aurora.name` | Place name *(apps only)* | names the place | Aurora | Also the name of a hand look (section 5) |
| `place.aurora.line` | Line under the place name *(apps only)* | describes it | A gust of light |   |
| `place.pool.name` | Place name | names the place | Pool |   |
| `place.pool.line` | Line under the place name | describes it | Liquid brain waves |   |
| `place.whirlpool.name` | Place name | names the place | Whirlpool |   |
| `place.whirlpool.line` | Line under the place name | describes it and teaches the gesture | A destroyer of worries |   |
| `place.planet.name` | Place name | names the place | Planet |   |
| `place.planet.line` | Line under the place name | describes it | Your home |   |
| `place.hyperspace.name` | Place name | names the place | Hyperspace |   |
| `place.hyperspace.line` | Line under the place name | describes it and teaches the gesture | Longest journey, shortest time |   |
| `place.julia.name` | Place name | names the place | Julia |   |
| `place.julia.line` | Line under the place name | describes it | Everything everywhere all at once |   |
| `place.marble.name` | Place name | names the place | Marble |   |
| `place.marble.line` | Line under the place name | describes it | Your way back to reality |   |

### 3c. The shape dial (centre of the Place ring)

The centre of the Place ring has one slider. What it does depends on the place, so its name and the words at its two ends change too.

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `place.dial.mirrors.label` | Slider name in Mandala, Tunnel, Liquid, Crystal, Dream, Circle limit, Dissolve, Julia, Marble and the sky places | how many mirrors | Mirrors | 1 word |
| `place.dial.mirrors.low` | Left end of that slider | few | Few | Dial end: 1 word |
| `place.dial.mirrors.high` | Right end of that slider | many | Many | Dial end: 1 word |
| `place.dial.fold.label` | Slider name in Fractal | how deep the folds go | Fold depth |   |
| `place.dial.fold.low` | Left end | shallow | Shallow | Dial end: 1 word |
| `place.dial.fold.high` | Right end | deep | Deep | Dial end: 1 word |
| `place.dial.repeat.label` | Slider name in Spiral | how tightly the picture repeats | Repeat ratio |   |
| `place.dial.repeat.low` | Left end | tight | Tight | Dial end: 1 word |
| `place.dial.repeat.high` | Right end | wide | Wide | Dial end: 1 word |
| `place.dial.tiles.label` | Slider name in Maze | how many tiles | Tiles |   |
| `place.dial.tiles.low` | Left end | few | Few | Dial end: 1 word |
| `place.dial.tiles.high` | Right end | many | Many | Dial end: 1 word |
| `place.dial.ring.label` | Slider name in Black hole | size of the bright ring | Ring size |   |
| `place.dial.ring.low` | Left end | small | Small | Dial end: 1 word |
| `place.dial.ring.high` | Right end | vast | Vast | Dial end: 1 word |
| `place.dial.ball.label` | Slider name in Glass ball | size of the ball | Ball size |   |
| `place.dial.ball.low` | Left end | small | Small | Dial end: 1 word |
| `place.dial.ball.high` | Right end | vast | Vast | Dial end: 1 word |
| `place.dial.pearl.label` | Slider name in Pearls | size of the pearls | Pearl size |   |
| `place.dial.pearl.low` | Left end | fine | Fine | Dial end: 1 word |
| `place.dial.pearl.high` | Right end | full | Full | Dial end: 1 word |
| `place.dial.poles.label` | Slider name in Poles | how fast it turns | Turning |   |
| `place.dial.poles.low` | Left end | still | Still | Dial end: 1 word |
| `place.dial.poles.high` | Right end | fast | Fast | Dial end: 1 word |
| `place.dial.pool.label` | Slider name in Pool | how rough the water is | Waves |   |
| `place.dial.pool.low` | Left end | glassy | Glassy | Dial end: 1 word |
| `place.dial.pool.high` | Right end | rough | Rough | Dial end: 1 word |
| `place.dial.whirl.label` | Slider name in Whirlpool | how strongly you stir | Stir |   |
| `place.dial.whirl.low` | Left end | gentle | Gentle | Dial end: 1 word |
| `place.dial.whirl.high` | Right end | strong | Strong | Dial end: 1 word |
| `place.dial.planet.label` | Slider name in Planet | how fast the planet turns | Turning |   |
| `place.dial.corridor.label` | Slider name in Hyperspace | how fast you fly | Speed |   |
| `place.dial.flow.low` | Left end of the Planet and Hyperspace sliders (shared) | still | Still | Dial end: 1 word. One text for both places |
| `place.dial.flow.high` | Right end of the Planet and Hyperspace sliders (shared) | fast | Fast | Dial end: 1 word. One text for both places |

## 4\. Colour & calm

### 4a. Colour ring

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `colour.core.line` | Centre of the Colour ring, under the colour's name | says what a colour is | Live your light | Compare `halo.home.colour.say` |
| `colour.palette.rainbow` | Name of a colour (title in the centre, tooltip, screen reader) | names it | Rainbow | See note A below the table |
| `colour.palette.neon` | Colour name | names it | Neon | See note A |
| `colour.palette.vapor` | Colour name | names it | Vapor | See note A |
| `colour.palette.sunset` | Colour name | names it | Sunset | See note A |
| `colour.palette.ember` | Colour name | names it | Ember | See note A |
| `colour.palette.acid` | Colour name | names it | Nectar | See note A |
| `colour.palette.toxic` | Colour name | names it | Healing | See note A |
| `colour.palette.ocean` | Colour name | names it | Coral | See note A |
| `colour.palette.ice` | Colour name | names it | Ice | See note A |
| `colour.palette.magenta` | Colour name | names it | Magenta | See note A |
| `colour.palette.mono` | Colour name | names it | Mono | See note A |

Note A: right now the colour names are not written out anywhere. The app makes them from internal code words by capitalising the first letter, so renaming one takes a small code change. Rename freely; the engineer will make it work.

### 4b. Calm ring (five moods)

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `calm.still.name` | Calm ring label, and the title in the centre | names the mood | Nothing | Ring label: 1 word |
| `calm.still.line` | Centre of the Calm ring | describes the mood and when to use it | Barely moving. To switch off. |   |
| `calm.soft.name` | Calm ring label and title | names the mood | Soft | Ring label: 1 word |
| `calm.soft.line` | Centre of the Calm ring | describes it | Slow, with gentle edges. For resting. |   |
| `calm.flow.name` | Calm ring label and title | names the mood | Flowing | Ring label: 1 word |
| `calm.flow.line` | Centre of the Calm ring | describes it | An easy current. For settling in. |   |
| `calm.bright.name` | Calm ring label and title | names the mood | Bright | Ring label: 1 word |
| `calm.bright.line` | Centre of the Calm ring | describes it | Clear and lively. For waking up. |   |
| `calm.wild.name` | Calm ring label and title | names the mood | Wild | Ring label: 1 word |
| `calm.wild.line` | Centre of the Calm ring | describes it | Fast and molten. For letting go. |   |
| `calm.core.finetune` | Small button in the centre of the Calm ring | opens four sliders for fine control | Fine tune | Small button |

### 4c. Fine tune (four sliders)

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `tune.pace.label` | Fine tune, slider 1 | overall speed | Pace |   |
| `tune.pace.low` | Left end | slow | Still | Dial end: 1 word |
| `tune.pace.high` | Right end | fast | Lively | Dial end: 1 word |
| `tune.echo.label` | Fine tune, slider 2 | how long light lingers | Echo |   |
| `tune.echo.low` | Left end | short | Fleeting | Dial end: 1 word |
| `tune.echo.high` | Right end | long | Lingering | Dial end: 1 word |
| `tune.melt.label` | Fine tune, slider 3 | how much the mirrors melt | Melt |   |
| `tune.melt.low` | Left end | sharp | Crisp | Dial end: 1 word |
| `tune.melt.high` | Right end | melting | Molten | Dial end: 1 word |
| `tune.glow.label` | Fine tune, slider 4 | how much it glows | Glow |   |
| `tune.glow.low` | Left end | soft | Soft | Dial end: 1 word |
| `tune.glow.high` | Right end | radiant | Radiant | Dial end: 1 word |

## 5\. Hands

### 5a. Hand looks (Hands ring)

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `hands.look.0.name` | Hands ring label, title in its centre, chip in All settings, toasts | names the look | Ink only | Ring label: 1–2 words |
| `hands.look.0.line` | Centre of the Hands ring | describes the look | Only what your fingers paint |   |
| `hands.look.1.name` | Hand look name | names the look | Constellation |   |
| `hands.look.1.line` | Centre of the Hands ring | describes it | Your hand in threads of starlight |   |
| `hands.look.2.name` | Hand look name | names the look | Neon | Also a colour name |
| `hands.look.2.line` | Centre of the Hands ring | describes it | Glowing tubes of light |   |
| `hands.look.3.name` | Hand look name | names the look | Aurora | Also a place name |
| `hands.look.3.line` | Centre of the Hands ring | describes it | Northern lights hang from your fingers |   |
| `hands.look.4.name` | Hand look name | names the look | Ribbons |   |
| `hands.look.4.line` | Centre of the Hands ring | describes it | Satin ribbons that flow and settle |   |
| `hands.look.5.name` | Hand look name | names the look | Aura |   |
| `hands.look.5.line` | Centre of the Hands ring | describes it | A corona that breathes with you |   |
| `hands.line.suffix.off` | Added to the end of a look's line while hand tracking is off | says the look only shows once Hands are on | · shown when Hands are on | Starts with `·`. Not added for "Ink only" |
| `hands.feed.on` | Small button in the centre of the Hands ring (hidden for "Ink only") | the look's light goes into the picture | Feeds the picture | Small button |
| `hands.feed.off` | Same button, other state | the look sits on top only | On top only | Small button |

### 5b. Hands button beside the orb

Each press moves to the next step: hands paint → hands paint and are drawn in light → off. The text is the button's tooltip and is also read by screen readers.

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `side.hands.off` | Tooltip on the hand button, while hands are off | invites you to paint with your hands | Paint with your hands | Short: tooltip |
| `side.hands.painting.next` | Tooltip while hands paint, if a look is chosen but not shown | says the next press shows the look | Hands are painting · press to show them as {look} |   |
| `side.hands.shown` | Tooltip while hands paint and are drawn as a look | says the next press turns hands off | Hands are painting, shown as {look} · press to turn off |   |
| `side.hands.painting.only` | Tooltip while hands paint and the look is "Ink only" | says the next press turns hands off | Hands are painting · press to turn off |   |

## 6\. Light

### 6a. Light ring: what paints

The title in the centre of the Light ring is the name of the pattern if one is chosen, or the name of the brush if not.

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `light.core.default` | Centre of the Light ring, before you touch anything | says what Light is about | What shines into the mirrors | Also shown when you turn Camera, Hands or Both hands off |
| `light.brush.neon.name` | Light ring label (brush 1), and in All settings → Brush | names the brush | Line | Ring label: 1 word. See note B below the table |
| `light.brush.neon.say` | Centre of the Light ring after choosing it | describes it | A line of light follows you |   |
| `light.brush.dots.name` | Light ring label (brush 2) | names the brush | Dots | Ring label: 1 word |
| `light.brush.dots.say` | Centre of the Light ring | describes it | Light falls in drops |   |
| `light.brush.sparks.name` | Light ring label (brush 3) | names the brush | Sparks | Ring label: 1 word |
| `light.brush.sparks.say` | Centre of the Light ring | describes it | Sparks scatter from your touch |   |
| `light.gen.none.name` | Light ring label (no pattern) | only your own painting, no pattern | Just you | Ring label. In All settings, the same choice is called "None" (`opt.none`) |
| `light.gen.none.say` | Centre of the Light ring | describes it | Only your own light |   |
| `light.gen.rings.name` | Light ring label (pattern) | names the pattern | Rings | Ring label: 1 word |
| `light.gen.rings.say` | Centre of the Light ring | describes it | Rings spread from the centre |   |
| `light.gen.petals.name` | Light ring label (pattern) | names the pattern | Petals |   |
| `light.gen.petals.say` | Centre of the Light ring | describes it | Petals open and close |   |
| `light.gen.flow.name` | Light ring label (pattern) | names the pattern | Flow lines |   |
| `light.gen.flow.say` | Centre of the Light ring | describes it | Light drifts like smoke |   |
| `light.gen.cells.name` | Light ring label (pattern) | names the pattern | Cells |   |
| `light.gen.cells.say` | Centre of the Light ring | describes it | Soft cells, slowly shifting |   |
| `light.gen.orbs.name` | Light ring label (pattern) | names the pattern | Orbs |   |
| `light.gen.orbs.say` | Centre of the Light ring | describes it | Lanterns circling |   |
| `light.gen.fractal3d.name` | Light ring label (pattern) | names the pattern | Lattice |   |
| `light.gen.fractal3d.say` | Centre of the Light ring | describes it | A crystal you fly through |   |

Note B: these brush and pattern names are also the choices in the All settings dropdowns (section 8d).

### 6b. Light ring: camera and hands switches

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `light.camera.name` | Light ring label (on/off) | turns the camera on | Camera | Ring label: 1 word |
| `light.camera.say` | Centre of the Light ring when Camera turns on | explains it | What the camera sees becomes light |   |
| `light.hands.name` | Light ring label (on/off) | turns hand tracking on | Hands | Ring label: 1 word |
| `light.hands.say` | Centre of the Light ring when Hands turns on | teaches the gestures | Paint in the air · raise a finger, pinch to hold the glass |   |
| `light.both.name` | Light ring label (on/off). Appears only while Hands is on | tracks two hands | Both hands | Ring label: 2 words |
| `light.both.say` | Centre of the Light ring when Both hands turns on | teaches two-handed use | Hold with one hand, paint with the other |   |
| `light.both.off` | Centre of the Light ring when Both hands turns off | explains the trade-off | One hand only · lighter on the laptop | "laptop" is wrong on a phone. Maybe a more general word? |

### 6c. Light ring centre

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `light.dial.wanderers.label` | Slider in the centre of the Light ring | how many lights paint by themselves | Lights that paint on their own |   |
| `light.dial.wanderers.low` | Left end | none | None | Dial end: 1 word |
| `light.dial.wanderers.high` | Right end | many | Many | Dial end: 1 word |
| `light.core.clear` | Small button in the centre of the Light ring | wipes the picture clean | Clear the glass | Small button |

## 7\. Keep (pictures, films, links)

### 7a. Keep ring

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `keep.core.title` | Title in the centre of the Keep ring | names the view | Keep |   |
| `keep.core.line` | Centre of the Keep ring, before you touch anything | invites you to save | Take this moment with you | Same words as `halo.home.keep.say` |
| `keep.core.keys` | Small button in the centre of the Keep ring | opens the keys and gestures sheet | Keys | Small button |
| `keep.picture.label` | Keep ring label | saves a picture | Picture | Ring label: 1 word |
| `keep.picture.say` | Centre of the Keep ring after tapping Picture | confirms it | Saved as an image |   |
| `keep.film.label` | Keep ring label (on/off) | records a film | Film | Ring label: 1 word |
| `keep.film.say` | Centre of the Keep ring after tapping Film | explains how to stop | Recording until you tap again |   |
| `keep.link.label` | Keep ring label | copies a link | Link | Ring label: 1 word |
| `keep.link.say` | Centre of the Keep ring after tapping Link | explains the link | A link back to exactly this |   |
| `keep.full.label` | Keep ring label (on/off) | full screen | Fill screen | Ring label: 2 words |
| `keep.full.say` | Centre of the Keep ring after tapping Fill screen | describes it | Nothing but the glass |   |

### 7b. Saved file names

These are the names your saved pictures and films get on disk. {place} is the place's short code name (for example `black-hole`) and {time} is the date and time.

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `file.picture` | Name of a saved picture | names the file | fractonaut-{place}-{time}.png | Keep the `.png` ending |
| `file.picture.hires` | Name of a saved 4K picture | names the file | fractonaut-{place}-{width}x{height}-{time}.png | Keep the `.png` ending |
| `file.film` | Name of a saved film | names the file | fractonaut-{place}-{time}.mp4 | The ending is `.webm` in some browsers |

### 7c. Keys and gestures sheet (opened from Keep → Keys, or by pressing ?)

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `keys.title` | Heading of the sheet | names the sheet | Keys and gestures |   |
| `keys.tap.key` | Left column | the gesture | Tap · click |   |
| `keys.tap.does` | Right column, beside "Tap · click" | what it does | Drop a ripple |   |
| `keys.scroll.key` | Left column | the gesture | Scroll · pinch |   |
| `keys.scroll.does` | Right column | what it does | Dive in or out |   |
| `keys.twist.key` | Left column | the gesture | Twist · Shift scroll |   |
| `keys.twist.does` | Right column | what it does | Spin the glass |   |
| `keys.air.key` | Left column | hand-tracking gestures | In the air |   |
| `keys.air.does` | Right column, beside "In the air" | explains every hand gesture | Raised fingers paint · a fist rests · pinch to hold the glass, then turn or push, and paint with the fingers you have left. With Both hands on (it appears beside Hands): hold with one hand and paint with the other, turn it like a wheel with both, or hold both palms still for the next chamber | Long. Uses "chamber" where the rest of the app says "place" |
| `keys.space.key` | Left column | the key | Space |   |
| `keys.space.does` | Right column, beside "Space" | what it does | Turn the glass |   |
| `keys.numbers.does` | Right column, beside "1 – 9 · ← →" | what it does | Chambers | Uses "chambers" where the rest of the app says "places" |
| `keys.updown.does` | Right column, beside "↑ ↓" | what it does | More or fewer mirrors |   |
| `keys.w.does` | Right column, beside "W" | what it does | Wander | The ring calls this "Drift" |
| `keys.bp.does` | Right column, beside "B · P" | what it does | Next brush · next palette | The ring calls palettes "Colour" |
| `keys.ct.does` | Right column, beside "C · T" | what it does | Camera · fingertips | The ring calls this "Hands" |
| `keys.sm.does` | Right column, beside "S · M" | what it does | Sound · listen to music |   |
| `keys.x.does` | Right column, beside "X" | what it does | Clear the glass |   |
| `keys.ir.does` | Right column, beside "I · R" | what it does | Save image · record video |   |
| `keys.l.does` | Right column, beside "L" | what it does | Copy link |   |
| `keys.oe.does` | Right column, beside "O · E" | what it does | Open the halo · all settings | "halo" is our internal word for the ring |
| `keys.g.does` | Right column, beside "G" | what it does | Breathe with the orb |   |
| `keys.hf.does` | Right column, beside "H · F" | what it does | Hide controls · full screen |   |
| `keys.close` | Button at the bottom of the sheet | closes it | Close |   |

## 8\. All settings

All settings is the full list behind the Settings circle. Each group is a heading you can open or close. Under each heading, a grey line shows the first three setting names in it. The app builds that line itself, so it has no copy of its own.

### 8a. Sheet

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `settings.title` | Heading of the settings sheet | names it | All settings |   |
| `settings.search` | Grey hint inside the search box | invites a search | Search settings |   |
| `settings.nohits.title` | Shown when a search finds nothing | says nothing matched | No setting by that name |   |
| `settings.nohits.hint` | Under that | suggests words to try | Try speed, color or glow | Should name real settings |

### 8b. Group headings (in the order they appear)

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `settings.group.hands` | Group heading 1 (open by default) | hand looks | Hands |   |
| `settings.group.chamber` | Group heading 2 | the shape of the place | Chamber | The ring says "Place" |
| `settings.group.light` | Group heading 3 | brushes and wanderers | Light |   |
| `settings.group.pattern` | Group heading 4 | background patterns | Pattern |   |
| `settings.group.camera` | Group heading 5 | camera | Camera |   |
| `settings.group.color` | Group heading 6 | colour and time | Color & time |   |
| `settings.group.echo` | Group heading 7 | how light lingers | Echo |   |
| `settings.group.finish` | Group heading 8 | final look | Finish |   |
| `settings.group.devices` | Group heading 9 | controllers and sensors | Devices |   |
| `settings.group.journey` | Group heading 10 | drift timing and performance | Journey & performance |   |

### 8c. Setting names, group by group

These names also show up in the "Knob controls" dropdown and in a few toasts (section 10). Some appear only in one place; the Where column says which.

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `set.hands.look` | Hands group: row of chips (the look names from 5a) | picks the hand look | Hands look |   |
| `set.hands.feed` | Hands group: switch | look's light goes into the picture | Feeds the picture | Same as `hands.feed.on` |
| `set.mode` | Chamber group: dropdown | type of mirrors | Mirror shape |   |
| `set.symmetry` | Chamber group: slider | how many mirrors | Mirrors |   |
| `set.mirror` | Chamber group: switch | reflect as well as rotate | Reflect, not only rotate |   |
| `set.foldInLoop` | Chamber group: switch | fold the trailing light too | Fold the echoes too |   |
| `set.spinSpeed` | Chamber group: slider | how fast mirrors turn | Mirror turning °/s | "°/s" means degrees per second |
| `set.viewZoom` | Chamber group: slider | zoom | Lens zoom |   |
| `set.fisheye` | Chamber group: slider | fisheye bulge | Lens bulge |   |
| `set.warpAmount` | Chamber group: slider | melting | Melt |   |
| `set.warpScale` | Chamber group: slider | size of the melting | Melt size |   |
| `set.warpSpeed` | Chamber group: slider | speed of the melting | Melt speed |   |
| `set.placeAmount` | Chamber group: slider | how strongly your finger steers in Pool, Whirlpool and similar places | Strength (places a finger steers) |   |
| `set.kifsIters` | Chamber group, only in Fractal | how deep the folds go | Fold depth |   |
| `set.kifsScale` | Chamber group, only in Fractal | fold scale | Fold scale |   |
| `set.kifsAngle` | Chamber group, only in Fractal | twist | Twist |   |
| `set.kifsAngleSpeed` | Chamber group, only in Fractal | twist speed | Twist speed °/s |   |
| `set.kifsOffX` | Chamber group, only in Fractal | sideways offset | Offset across |   |
| `set.kifsOffY` | Chamber group, only in Fractal | upward offset | Offset up |   |
| `set.hypQ` | Chamber group, only in Circle limit and Dissolve | tiles per corner | Tiles meeting at a corner |   |
| `set.hypDrift` | Chamber group, only in Circle limit and Dissolve | drift | Drift |   |
| `set.drosteScale` | Chamber group, only in Spiral | how tightly the picture repeats | Repeat ratio |   |
| `set.drosteSpeed` | Chamber group, only in Spiral | zoom speed | Zoom speed |   |
| `set.tileScale` | Chamber group, only in Maze | number of tiles | Tiles |   |
| `set.lensRing` | Only in the "Knob controls" dropdown (not shown as a row) | size of the black hole's ring | Ring size |   |
| `set.pearlScale` | Only in the "Knob controls" dropdown (not shown as a row) | pearl size | Pearl size |   |
| `set.brush` | Light group: dropdown | brush type | Brush |   |
| `set.inkHover` | Light group: switch | paint by just moving the mouse | Paint without clicking |   |
| `set.inkSize` | Light group: slider | brush size | Brush size |   |
| `set.inkGlow` | Light group: slider | brush glow | Brush glow |   |
| `set.inkBright` | Light group: slider | brush brightness | Brush brightness |   |
| `set.inkSpeed` | Light group: slider | fast strokes get thicker | Speed thickens strokes |   |
| `set.inkColor` | Light group: dropdown | how the brush picks colour | Brush color |   |
| `set.autoPens` | Light group: slider | number of self-painting lights | Wanderers | The Light ring calls these "Lights that paint on their own" |
| `set.autoPath` | Light group: dropdown | the wanderers' path | Wanderer path |   |
| `set.autoSpeed` | Light group: slider | wanderer speed | Wanderer speed |   |
| `set.ripple` | Light group: slider | ripples on tap | Tap ripples |   |
| `set.stars` | Light group: slider | star field | Stars |   |
| `set.starKind` | Light group: dropdown | which sky | Sky |   |
| `set.gen` | Pattern group: dropdown | background pattern | Pattern |   |
| `set.genMix` | Pattern group: slider | pattern strength | Strength |   |
| `set.genScale` | Pattern group: slider | pattern size | Size |   |
| `set.genSpeed` | Pattern group: slider | pattern speed | Speed |   |
| `set.genRadius` | Pattern group: slider | where the pattern comes from | Source size (0 = everywhere) |   |
| `set.rmScale` | Pattern group: slider | lattice folding | Lattice fold |   |
| `set.rmFly` | Pattern group: slider | flight speed through the lattice | Lattice flight speed |   |
| `set.camera.device` | Camera group: dropdown (first row) | which camera | Camera to use |   |
| `set.camera.auto` | Camera group: first choice in that dropdown | let the app pick | Automatic |   |
| `set.camera.note` | Camera group: greyed-out last choice in that dropdown, before the camera has been turned on | explains why the list is short | Others appear once the camera is on | Web only |
| `set.camera.unnamed` | Camera group: a camera's name when the browser doesn't give one | fallback name | Camera {number} |   |
| `set.camMix` | Camera group: slider | camera strength | Strength |   |
| `set.camZoom` | Camera group: slider | camera zoom | Zoom |   |
| `set.camColorize` | Camera group: slider | recolour the camera | Recolor |   |
| `set.pace` | Color & time group: slider | overall speed | Pace |   |
| `set.palette` | Color & time group: dropdown | colour set | Palette | The ring calls this "Colour" |
| `set.paletteSpeed` | Color & time group: slider | colours drift over time | Palette drift |   |
| `set.paletteMap` | Color & time group: slider | recolour the whole picture | Recolor everything |   |
| `set.breath` | Color & time group: slider | gentle breathing motion | Breathing |   |
| `set.fbDecay` | Echo group: slider | how long light lingers | Echo length |   |
| `set.fbZoom` | Echo group: slider | light flows in or out | Flow inward ↔ outward |   |
| `set.fbRotate` | Echo group: slider | light flows round | Flow rotation °/s |   |
| `set.fbSwirl` | Echo group: slider | swirl | Swirl |   |
| `set.fbWarp` | Echo group: slider | shimmer | Shimmer |   |
| `set.fbHueShift` | Echo group: slider | colour drifts | Color drift °/s |   |
| `set.fbSat` | Echo group: slider | colour strength | Saturation |   |
| `set.fbSharpen` | Echo group: slider | sharpen | Sharpen |   |
| `set.fbDream` | Echo group: slider | slow dreamy current | Dream current |   |
| `set.fbComb` | Echo group: slider | strokes drag paint like wet paint | Wet paint |   |
| `set.bloom` | Finish group: slider | glow | Glow |   |
| `set.bloomThreshold` | Finish group: slider | what starts to glow | Glow threshold |   |
| `set.chroma` | Finish group: slider | rainbow fringe | Prism edges |   |
| `set.sheen` | Finish group: slider | liquid shine | Liquid sheen |   |
| `set.grain` | Finish group: slider | film grain | Grain |   |
| `set.vignette` | Finish group: slider | dark edges | Vignette |   |
| `set.exposure` | Finish group: slider | brightness | Brightness |   |
| `set.dev.midi` | Devices group: switch | use a MIDI controller | MIDI controller | Web only |
| `set.dev.knobs` | Devices group: dropdown | pick a setting for the next knob | Knob controls | Its choices are the setting names in this table |
| `set.dev.knobs.assign` | Devices group: button under Knob controls | assign the next knob turned | Assign next knob | Small button |
| `set.dev.knobs.forget` | Devices group: button under Knob controls | clear knob assignments | Forget knobs | Small button |
| `set.dev.twohands` | Devices group: switch | track two hands | Track two hands | Same as "Both hands" on the Light ring |
| `set.dev.handscpu` | Devices group: switch | track hands without the graphics chip | Track hands on the processor |   |
| `set.dev.handrate` | Devices group: switch | shows a technical readout of hand tracking | Show hand tracking rate | For testing |
| `set.dev.tilt` | Devices group: switch | steer by tilting the phone | Tilt to steer |   |
| `set.journey.interval` | Journey & performance group: slider | how often Drift moves on | Wander to a new view every | Followed by the number of seconds |
| `set.journey.morph` | Journey & performance group: slider | how long changes take | Transition length |   |
| `set.journey.flash` | Journey & performance group: switch | protects against flashing (photosensitivity) | Soften flashes |   |
| `set.journey.ceiling` | Journey & performance group: slider | brightness limit | Brightness ceiling |   |
| `set.journey.smooth` | Journey & performance group: switch | lowers detail to stay smooth | Keep motion smooth |   |
| `set.journey.cool` | Journey & performance group: switch | saves heat while tracking hands | Half frame rate while tracking hands |   |
| `set.journey.res` | Journey & performance group: slider | resolution | Resolution |   |
| `set.journey.dpr` | Journey & performance group: slider | pixel density limit | Pixel density limit |   |

### 8d. Choices inside the dropdowns

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `opt.none` | Choice in Mirror shape and in Pattern | no mirrors / no pattern | None | Also shows in the Palette dropdown. See note C below the table |
| `opt.kaleido` | Mirror shape choice | classic mirrors | Mandala mirrors |   |
| `opt.kifs` | Mirror shape choice | fractal | Fractal folds |   |
| `opt.hyperbolic` | Mirror shape choice | hyperbolic tiling | Circle limit |   |
| `opt.droste` | Mirror shape choice | picture-in-picture | Spiral |   |
| `opt.tiles` | Mirror shape choice | tiled | Maze |   |
| `opt.lens` | Mirror shape choice | black hole | Black hole |   |
| `opt.wormhole` | Mirror shape choice | glass ball | Glass ball |   |
| `opt.pearls` | Mirror shape choice | pearls | Pearls |   |
| `opt.poles` | Mirror shape choice | poles | Poles and zeros | The place is just "Poles" |
| `opt.pool` | Mirror shape choice | pool | Pool |   |
| `opt.whirl` | Mirror shape choice | whirlpool | Whirlpool |   |
| `opt.planet` | Mirror shape choice | planet | Planet |   |
| `opt.corridor` | Mirror shape choice | corridor | Hyperspace |   |
| `opt.julia` | Mirror shape choice | julia | Julia |   |
| `opt.galaxy` | Sky choice | galaxy | Galaxy |   |
| `opt.nebula` | Sky choice | nebula | Nebula |   |
| `opt.pulsar` | Sky choice | pulsar | Pulsar |   |
| `opt.quasar` | Sky choice | quasar | Quasar |   |
| `opt.supernova` | Sky choice | supernova | Supernova |   |
| `opt.aurora` | Sky choice | aurora | Aurora |   |
| `opt.rings` | Pattern choice (and Light ring label) | rings | Rings | Same as `light.gen.rings.name` |
| `opt.petals` | Pattern choice (and Light ring label) | petals | Petals | Same as `light.gen.petals.name` |
| `opt.flow` | Pattern choice (and Light ring label) | flow lines | Flow lines | Same as `light.gen.flow.name` |
| `opt.cells` | Pattern choice (and Light ring label) | cells | Cells | Same as `light.gen.cells.name` |
| `opt.orbs` | Pattern choice (and Light ring label) | orbs | Orbs | Same as `light.gen.orbs.name` |
| `opt.fractal3d` | Pattern choice (and Light ring label) | lattice | Lattice | Same as `light.gen.fractal3d.name` |
| `opt.neon` | Brush choice (and Light ring label) | line brush | Line | Same as `light.brush.neon.name`. See note C |
| `opt.dots` | Brush choice (and Light ring label) | dots brush | Dots | Same as `light.brush.dots.name` |
| `opt.sparks` | Brush choice (and Light ring label) | sparks brush | Sparks | Same as `light.brush.sparks.name` |
| `opt.cycle` | Brush color choice | colour shifts over time | Shifting |   |
| `opt.speed` | Brush color choice | colour by speed | By speed |   |
| `opt.angle` | Brush color choice | colour by direction | By angle |   |
| `opt.solid` | Brush color choice | one colour | Steady |   |
| `opt.lissajous` | Wanderer path choice | weaving path | Weave |   |
| `opt.rose` | Wanderer path choice | petal path | Petal |   |
| `opt.noise` | Wanderer path choice | drifting path | Drift |   |
| `opt.spiral` | Wanderer path choice | spiral path | Spiral |   |

Note C: the Palette dropdown in All settings lists the colour names from 4a, except that the colour "Neon" currently shows as **"Line"** there (it borrows the brush's name). That is a known bug for the engineer, not copy to edit.

### 8e. Numbers and readouts

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `num.seconds` | Number beside sliders measured in time | seconds | {n} s |   |
| `num.forever` | Echo slider's number when light never fades | forever | ∞ |   |
| `num.times` | Number beside Pace, Repeat ratio, Pixel density limit | multiplier | {n}× |   |
| `num.percent` | Number beside Melt, Glow, Waves, Stir, Resolution, Brightness ceiling | percent | {n}% |   |
| `set.readout` | Small grey line at the bottom of Journey & performance | how smoothly it runs | {fps} of {hz} fps · {width} × {height} |   |
| `set.readout.reduced` | Added to the end of that line when detail is lowered | explains the lowered detail | · {percent}% for smoothness |   |

## 9\. Visit — the palace story

The Visit starts on its own the first time someone opens the app (unless they arrived by a shared link), and from the Visit circle on the main ring. Lines appear one at a time near the top of the screen, each for about 7 seconds. Tapping the picture skips ahead through the opening lines.

### 9a. Arrival (in order)

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `visit.arrive.1` | Story line 1 | sets the scene | They say this palace shows itself only to the tired. | One sentence |
| `visit.arrive.2` | Story line 2 | the journey here | You have walked a long road to find its door. |   |
| `visit.arrive.3` | Story line 3 | permission to rest | Set down what you carry. No one here is waiting on you. |   |
| `visit.arrive.4` | Story line 4 | explains the rooms | Every room keeps one question, and a little light to answer it with. |   |
| `visit.arrive.5` | Story line 5 | explains what to do | Let the question sit with you. Then move your hands, and watch. |   |

### 9b. The five rooms (in order)

In each room: the door line appears, the picture dusks into the room's place, the question appears, and a small glass (flask) fills as you paint. When it is full, the potion line appears, then the box to write in.

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `visit.room1.door` | Story line, walking to room 1 (place: Mandala) | introduces the room | The first room is round, and it has been expecting you. |   |
| `visit.room1.q` | The room's question (shown larger) | the reflection prompt | What have I carried this far? |   |
| `visit.room1.potion` | Potion name, when the glass is full; also on the shelf | names what you gathered | Endurance | 1 word |
| `visit.room2.door` | Story line, walking to room 2 (place: Dream) | introduces the room | Down the hall is a room where everything drifts. |   |
| `visit.room2.q` | The room's question | the reflection prompt | What comes easily to me? |   |
| `visit.room2.potion` | Potion name | names what you gathered | Ease | 1 word |
| `visit.room3.door` | Story line, walking to room 3 (place: Black hole) | introduces the room | A stair leads down to a room with no floor, only a deep that pulls. |   |
| `visit.room3.q` | The room's question | the reflection prompt | What do I keep returning to? |   |
| `visit.room3.potion` | Potion name | names what you gathered | Devotion | 1 word |
| `visit.room4.door` | Story line, walking to room 4 (place: Liquid) | introduces the room | The next room will not hold still for anyone. |   |
| `visit.room4.q` | The room's question | the reflection prompt | When did I last surprise myself? |   |
| `visit.room4.potion` | Potion name | names what you gathered | Courage | 1 word |
| `visit.room5.door` | Story line, walking to room 5 (place: Circle limit) | introduces the room | The last door opens on a room with no far wall. |   |
| `visit.room5.q` | The room's question | the reflection prompt | Who is glad that I exist? |   |
| `visit.room5.potion` | Potion name | names what you gathered | Belonging | 1 word |

### 9c. In every room, once the glass is full

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `visit.full` | Line under the flask when the glass fills | names the potion | Something has gathered in the glass. The room calls it {potion}. | {potion} is shown in bold |
| `visit.form.placeholder` | Grey hint inside the writing box | invites a line of your own | Write what came to you, if anything did | Box holds up to 140 characters |
| `visit.form.bottle` | Button beside the box | saves your line to the shelf | Bottle it | Small button |
| `visit.form.skip` | Second button | moves on without writing | Leave it unwritten | Small button |
| `visit.kept.written` | Story line after "Bottle it" | confirms it is kept | It goes on the shelf, still warm. |   |
| `visit.kept.unwritten` | Story line after "Leave it unwritten" | reassures | You leave it unnamed. It keeps all the same. |   |

### 9d. Ending (after room 5)

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `visit.end.1` | Story line | sums up | Five small bottles, and every one of them yours. | Says "five": keep it if rooms change |
| `visit.end.2` | Story line (stays about 30 seconds, then the story closes) | invites a return | The palace keeps no hours. Come back whenever you are tired. |   |
| `visit.end.shelf` | Button that appears with the last line | opens the shelf | Go to the shelf | Small button |

### 9e. The shelf

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `shelf.title` | Heading of the shelf | names it | The shelf |   |
| `shelf.sub` | Line under the heading | explains it | What the rooms gave you, in your own words |   |
| `shelf.empty` | Shown on the shelf before any bottle | invites a visit | Nothing is bottled yet. Walk the rooms, and the shelf will fill. |   |
| `shelf.back` | Button at the bottom | closes the shelf | Back to the palace | Small button |

Each bottle on the shelf shows the potion name (9b), the room's question (9b), your own words, and the date (written in the device's own language).

## 10\. Toasts & messages

A toast is the short message that slides up from the bottom for a few seconds. Some toasts have a button on the right (the "action").

### 10a. Painting, sound and the breathing guide

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `toast.glass.cleared` | Toast after clearing the glass | confirms it | Glass cleared | One short line |
| `toast.glass.cleared.action` | Button on that toast | brings the picture back | Undo | 1 word |
| `toast.sound.back` | Toast when sound returns on a later visit | says sound is on | Sound is on |   |
| `toast.sound.back.action` | Button on that toast | turns it off | Turn off | 1–2 words |
| `toast.sound.key.on` | Toast after pressing S | sound on | Sound on |   |
| `toast.sound.key.off` | Toast after pressing S | sound off | Sound off |   |
| `toast.listen.on` | Toast after turning on Listen | invites music | Listening · play some music |   |
| `toast.drift.first` | Toast the first time Drift is turned on | explains Drift | A new view every {seconds} s |   |
| `toast.controls.hidden` | Toast after pressing H | how to get controls back | Controls hidden · press H to bring them back |   |
| `toast.cycle` | Toast after pressing B or P | names the new choice | {setting} · {choice} | e.g. "Brush · Sparks", "Palette · Ocean" |
| `whisper.breathe.in` | Quiet line at the top, during the first three breaths of the breathing guide | breathe in | Breathe in |   |
| `whisper.breathe.out` | Same place | breathe out | Breathe out |   |
| `toast.unavailable` | Toast when a switch on the ring can't be turned on and there is no better reason to give | fallback | That isn't available here |   |

### 10b. Hands

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `toast.hands.loading` | Toast while hand tracking starts | wait a moment | Loading fingertip tracking… | Web only |
| `toast.hands.ready` | Toast when hand tracking is ready | teaches the first gesture | Hands ready · raise a finger to paint |   |
| `toast.hands.on` | Toast after the hand button beside the orb turns hands on | teaches the gestures | Raise a finger to paint · pinch to hold the glass |   |
| `toast.hands.look` | Toast when the hand button shows a look | names the look | Hands: {look} |   |
| `toast.hands.look.action` | Button on that toast | opens the Hands ring | Change | 1 word |
| `toast.hands.failed` | Toast when hand tracking fails | says why | Fingertip tracking couldn't start · check your connection | In the apps no internet is needed |
| `toast.camera.disconnected` | Toast when the camera is unplugged and nothing else can take over | says so | The camera was disconnected | Web only. Apps: `native.toast.camera.stopped` |

### 10c. Pictures, films and links

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `toast.picture.saved` | Toast after saving a picture | confirms it | Image saved to downloads | On Android it actually goes to Pictures/fractonaut |
| `toast.picture.saved.action` | Button on that toast | saves a bigger version | Save in 4K | Short |
| `toast.picture.rendering` | Toast while the 4K picture is made | wait | Rendering {width} × {height}… |   |
| `toast.picture.hires.saved` | Toast when the 4K picture is saved | confirms it | {width} × {height} image saved to downloads |   |
| `toast.picture.hires.failed` | Toast when the 4K picture can't be made | says why | Not enough graphics memory · try a smaller size | No smaller size is offered |
| `toast.film.started.sound` | Toast when recording starts with sound | how to stop | Recording with sound · press R to stop | Phones have no R key |
| `toast.film.started` | Toast when recording starts without sound | how to stop | Recording · press R to stop | Phones have no R key |
| `toast.film.finishing` | Toast while the film is wrapped up | wait | Finishing the film… | Web only |
| `toast.film.saved` | Toast when the film is saved | confirms it | Video saved to downloads | On Android it goes to Movies/fractonaut |
| `toast.film.saved.action` | Button on that toast (phones and the apps) | share or save to Photos | Share or save to Photos |   |
| `toast.film.saved.webm` | Toast when the film is saved in the WebM format | confirms it, with a caution | Video saved to downloads · WebM, which some galleries cannot open | Web only |
| `toast.film.nobrowser` | Toast when the browser can't record | says why | This browser can't record video · try Chrome | Web only |
| `toast.film.dead` | Toast when recording produced nothing | says why | Recording stopped · this browser was not producing a film | Web only |
| `toast.film.slow` | Toast when the device records slowly | warns | This device is encoding slowly · the film will skip rather than run out of memory | Web only. Long |
| `toast.film.cantkeepup` | Toast when recording must end early | says why | This device could not keep up with filming, so the film ends here | Web only |
| `toast.film.error` | Toast when recording fails | says why | Recording stopped · the browser reported an error | The apps show this too, though there is no browser in them |
| `toast.link.copied` | Toast after copying a link | confirms it | Link copied · it opens this exact view |   |
| `toast.link.fallback` | Toast when copying fails | shows the link itself | {link} | Not copy: the web address itself |

### 10d. Knobs and controllers (web only)

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `toast.midi.connected` | Toast when a MIDI controller connects | confirms it | MIDI connected · {count} device{s} · keys switch chambers | {s} is "s" for more than one device |
| `toast.knob.learn` | Toast after "Assign next knob" | instructs | Turn a knob to control {setting} | {setting} is the setting name in lower case |
| `toast.knob.assigned` | Toast once a knob is assigned | confirms it | Knob {number} now controls {setting} |   |
| `toast.knobs.forgotten` | Toast after "Forget knobs" | confirms it | Knob assignments forgotten · reload to use the defaults |   |

## 11\. Errors & permissions

These show as toasts unless the Where column says otherwise.

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `err.camera.https` | Toast when turning on the camera or hands on an insecure page | says why it can't | The camera needs HTTPS or localhost | Technical. Web only |
| `err.camera.open` | Toast when the chosen camera can't open | says so | That camera couldn't be opened | Web only |
| `err.camera.denied` | Toast when camera permission is refused | says why | (the browser's own wording, e.g. "Permission denied") | We have no copy here yet. Write one if you'd like |
| `err.listen.https` | Toast when turning on Listen on an insecure page | says why | Listening needs HTTPS or localhost | Technical. Web only |
| `err.listen.permission` | Toast when microphone permission is refused | how to fix | Listening needs microphone access · allow it in the address bar | The apps show this too, though they have no address bar |
| `err.sound.none` | Toast when the browser can't play sound | says why | This browser can't play sound | Web only |
| `err.midi.none` | Toast when the browser has no MIDI | says why | This browser has no MIDI support · try Chrome or Edge | Web only |
| `err.tilt.none` | Toast when turning on Tilt to steer without a tilt sensor | says why | This device has no tilt sensor | Also shown in the apps |
| `err.tilt.permission` | Toast when motion permission is refused | how to fix | Tilt needs motion access · allow it in settings |   |
| `err.gpu.lost` | Toast when the graphics chip resets | how to fix | GPU context lost — reload the page | Technical |
| `err.webgl` | Full-screen message instead of the app, when the browser can't draw it | says why | WebGL2 is not available in this browser. | Technical. Only one shown in the centre of an empty screen |

## 12\. Screen-reader labels

Read aloud by screen readers. Never shown on screen.

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `sr.canvas` | The picture itself (screen reader) | describes the main canvas | fractonaut canvas. Move to paint with light. |   |
| `sr.orb.rest` | The orb, while the ring is closed (screen reader) | names the orb | Controls |   |
| `sr.orb.back` | The orb, inside a sub-ring (screen reader) | the orb goes back | Back |   |
| `sr.orb.close` | The orb, on the main ring or settings (screen reader) | the orb closes | Close |   |
| `sr.halo` | The ring as a whole (screen reader) | names the ring | Controls |   |
| `sr.ring.colour` | The Colour ring (screen reader) | names the group | Colour |   |
| `sr.ring.hands` | The Hands ring, and the chips in All settings → Hands (screen reader) | names the group | Hands look |   |
| `sr.ring.calm` | The Calm ring (screen reader) | names the group | Calm |   |
| `sr.brand` | The logo at the centre of the main ring (screen reader) | reads the logo | fractonaut 2026 |   |
| `sr.welcome` | The welcome caption (screen reader) | reads the greeting | Welcome, fractonaut | Should match `brand.welcome.prefix` + logo |
| `sr.places.close` | Close button on the wall of places (screen reader) | names it | Close |   |
| `sr.settings.close` | Close (×) button on All settings (screen reader) | names it | Close | Icon only on screen |
| `sr.visit.flask` | The filling glass in the Visit (screen reader) | says what it is | The glass filling | Read with a percentage |
| `sr.visit.input` | The writing box in the Visit (screen reader) | names the box | Write what came to you |   |
| `sr.swatch` | Each colour circle (screen reader and tooltip) | the colour's name | {colour} | Comes from 4a |
| `sr.side.sound` | Speaker button beside the orb (screen reader) | same as its tooltip | (see `side.sound.on` / `side.sound.off`) |   |
| `sr.side.hands` | Hand button beside the orb (screen reader) | same as its tooltip | (see `side.hands.*` in 5b) |   |

## 13\. Native apps (Mac and Android)

The Mac and Android apps show the same words as the web app, except for the items below.

### 13a. App names and windows

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `native.mac.name` | Mac: app name in the Dock, Finder and menu bar | names the app | fractonaut |   |
| `native.mac.window` | Mac: window title (hidden, but used by Mission Control and the Window menu) | names the window | fractonaut |   |
| `native.android.name` | Android: name under the home-screen icon | names the app | fractonaut |   |
| `native.android.name.dev` | Android: name of the test build | names the test app | fractonaut dev | For testing |
| `native.android.folder` | Android: folder in Pictures and in Movies where saves go | names the folder | fractonaut |   |

### 13b. Mac permission prompts

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `native.mac.perm.camera` | macOS prompt the first time the camera is used | explains why we ask | The camera lets you paint with your hands, or shine its picture into the glass. Nothing leaves your Mac. |   |
| `native.mac.perm.mic` | macOS prompt the first time Listen is used | explains why we ask | The microphone lets music in the room move the picture. Nothing leaves your Mac. |   |

Android shows its own standard permission prompts, which we can't change.

### 13c. Messages that differ in the apps

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `native.toast.camera.stopped` | Toast when the camera stops (Mac, Android) | says so | The camera stopped | Same idea as `toast.camera.disconnected` on the web |
| `native.err.camera` | Toast when the camera can't be turned on (Mac, Android) | says so | That isn't available here | Same words as `toast.unavailable` |
| `native.camera.front` | Android: camera name in Camera to use | names it | Front camera |   |
| `native.camera.back` | Android: camera name | names it | Back camera |   |
| `native.camera.external` | Android: camera name | names it | External camera |   |
| `native.camera.numbered` | Android: camera name when there are several of one kind | names it | {kind} {number} | e.g. "Back camera 2" |
| `native.camera.unnamed` | Mac and Android: camera name if the system gives none | fallback | Camera |   |

### 13d. Mac menu bar

Standard macOS wording. Change it only if you have a reason to.

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `native.mac.menu.hide` | App menu | hide app | Hide {app} | {app} is "fractonaut" |
| `native.mac.menu.hideothers` | App menu | hide other apps | Hide Others |   |
| `native.mac.menu.showall` | App menu | show all apps | Show All |   |
| `native.mac.menu.quit` | App menu | quit | Quit {app} |   |
| `native.mac.menu.edit` | Menu bar | Edit menu title | Edit |   |
| `native.mac.menu.undo` | Edit menu | undo | Undo |   |
| `native.mac.menu.redo` | Edit menu | redo | Redo |   |
| `native.mac.menu.cut` | Edit menu | cut | Cut |   |
| `native.mac.menu.copy` | Edit menu | copy | Copy |   |
| `native.mac.menu.paste` | Edit menu | paste | Paste |   |
| `native.mac.menu.selectall` | Edit menu | select all | Select All |   |
| `native.mac.menu.window` | Menu bar | Window menu title | Window |   |
| `native.mac.menu.minimize` | Window menu | minimize | Minimize |   |
| `native.mac.menu.zoom` | Window menu | zoom | Zoom |   |
| `native.mac.menu.fullscreen` | Window menu | full screen | Enter Full Screen |   |

## 14\. Developer-only (for reference; not seen in normal use)

| ID | Where it appears | What it's for | Text | Notes |
| --- | --- | --- | --- | --- |
| `dev.handrate` | Small readout, only when "Show hand tracking rate" is on | testing | hands {count} · {hz} Hz |   |
| `dev.shader` | Full-screen message if the graphics code fails to build | testing | Shader compile failed: {details} |   |
| `dev.link` | Full-screen message if the graphics code fails to link | testing | Link failed: {details} |   |

* * *

## Appendix: source anchors (for the engineer)

Files: **I** = `index.html`, **S** = `mac/web/shim.js`, **U** = `mac/tools/make_ui.mjs` (patches I's UI block for the apps: when changing a string it anchors on, update the anchor too), **P** = `mac/app/Info.plist`, **MM** = `mac/app/main.mm`, **V** = `android/app/src/main/kotlin/app/fractonaut/Vision.kt`, **G** = `android/app/build.gradle.kts`, **MA** = `android/app/src/main/kotlin/app/fractonaut/MainActivity.kt`, **ME** = `android/app/src/main/kotlin/app/fractonaut/Media.kt`.

| ID | Source anchor |
| --- | --- |
| `brand.page.title` | I `<title>fractonaut 2026 · out on` |
| `brand.wordmark.word` | I `const WORD = '<span class="brand-word"` (fract + brand-o + naut) |
| `brand.wordmark.year` | I `const BRAND =` … `brand-year` |
| `brand.welcome.prefix` | I showCaption: `<span aria-hidden="true">Welcome, </span>` |
| `brand.welcome.tagline` | I showCaption: `l.textContent = 'fractonaut 2026 ·` |
| `brand.home.say` | I `const HOME_SAY =` |
| `halo.home.*.label` / `.say` | I buildHome `const items = [` … `{ face: 'place', label: 'Place'` etc. (label/say per item) |
| `side.sound.on/off` | I buildPanels `sideSound.title = snd ? 'Sound is on' : 'Turn sound on'` |
| `side.hands.*` | I buildPanels `sideHands.title = !hands ? 'Paint with your hands'` (one nested ternary, 4 strings) |
| `place.ring.all` | I buildChambers `pip('go', … 'All places'` |
| `place.core.turn` | I `<button class="link" id="turn">` |
| `place.wall.title` / `.close` | I `<h2 id="h-places">` / its `aria-label="Close">Close` |
| `place.wall.group.*` | I buildChambers `const GROUPS = [ ['The sky'`; `section('More'` |
| `place.<slug>.name` / `.line` | I `const CHAMBERS = [` → `{ name: '…', slug: '<slug>', line: '…'` |
| `place.dial.*` | I renderShapeDial `const specs = {` (intDial/label/ends); `strength('Waves'`, `strength('Stir'`, `flow('Turning')`, `flow('Speed')`, `const flow = (label) => ({ label, ends: ['Still', 'Fast']` |
| `colour.core.line` | I `<p class="core-line">The light you are sitting in` |
| `colour.palette.*` | I `const PALETTES = {` keys, displayed by `cap(p)` in buildMood (and `$('#paletteName').textContent = cap(`). Selection test `b.title.toLowerCase() === params.palette` breaks if titles are renamed |
| `calm.*.name` / `.line` | I `const CALM = [` |
| `calm.core.finetune` | I `id="fineTune">Fine tune` |
| `tune.*` | I buildMood `dial($('#paceDial')`, `#echoDial`, `#meltDial`, `#glowDial` (label/ends) |
| `hands.look.N.name` | I `const HAND_LOOKS = [` |
| `hands.look.N.line` | I `const HAND_LINE = [` |
| `hands.line.suffix.off` | I buildHands `' · shown when Hands are on'` |
| `hands.feed.on/off` | I buildHands `feed.textContent = system.handLookFeed ? 'Feeds the picture' : 'On top only'` |
| `light.core.default` | I buildLight `'What shines into the mirrors'` (2×: fallback in toggle click and `sayLight(` at end) |
| `light.brush.*.name`, `light.gen.*.name`, `opt.*` | I `const OPTION_NAMES = {` ; `g === 'none' ? 'Just you'` in buildLight |
| `light.brush.*.say`, `light.gen.*.say` | I `const LIGHT_SAY = {` |
| `light.camera/hands/both.*` | I buildLight `const toggles = [` (label/say/off) |
| `light.dial.wanderers.*` | I `label: 'Lights that paint on their own'` |
| `light.core.clear` | I `id="clearGlass">Clear the glass` |
| `keep.core.title/line/keys` | I `<h2 class="core-title">Keep</h2>`, `id="keepLine">`, `data-act="keys">Keys` |
| `keep.*.label` / `.say` | I buildKeep `const items = [` |
| `file.*` | I `` `fractonaut-${CHAMBERS[chamber].slug}-${stamp()}.png` `` (afterRender, also U patch 1), exportHiRes `-${w}x${h}-`, beginRecord ``const file = `fractonaut-${slug}`` (U patch 4 for apps) |
| `keys.*` | I `<dialog class="mn-sheet" id="keys"` `<dl class="keys` (dt/dd pairs) |
| `settings.title/search/nohits.*` | I `id="h-all">All settings`, `placeholder="Search settings"`, `No setting by that name`, `Try speed, color or glow` |
| `settings.group.hands` | I buildAll `group('Hands', [` |
| `settings.group.chamber…finish` | I `// ---------------- All settings` `const GROUPS = [` (first element of each pair) |
| `settings.group.devices` / `.journey` | I buildAll `group('Devices'`, `group('Journey & performance'` |
| `set.hands.look` | I handLookRow `settingRow('Hands look'` |
| `set.hands.feed` | I buildAll `switchRow('Feeds the picture'` |
| `set.<key>` (schema rows) | I `const SCHEMA = {` → `<key>: [...]` 6th element (label). Also feeds knob select, cycle toast, knob toasts |
| `set.lensRing` / `set.pearlScale` | I SCHEMA groups 'Black hole' / 'Pearls' are not in GROUPS, so they have no row; only in learnSel |
| `set.camera.*` | I cameraRow `settingRow('Camera to use'`, `'Automatic'`; Inputs `get camerasNote()` `'Others appear once'`; listCameras `` `Camera ${i + 1}` `` |
| `set.dev.*` | I buildAll Devices: `switchRow('MIDI controller'`, `settingRow('Knob controls'`, `'Assign next knob'`, `'Forget knobs'`, `'Track two hands'`, `'Track hands on the processor'`, `'Show hand tracking rate'`, `'Tilt to steer'` |
| `set.journey.*` | I buildAll `systemRange('autoVJInterval', 'Wander to a new view every'` … `systemRange('maxDpr', 'Pixel density limit'` |
| `num.*` | I `const echoSeconds =` (`'∞'`, `'0 s'`, `` ` s` ``); dial `out:` functions (`×`, `%`) |
| `set.readout*` | I ` readout.textContent = `` ` ${Math.round(stats.fps)} of\` \`\` |
| `visit.arrive.N` | I Visit `const ARRIVE = [` |
| `visit.roomN.door/q/potion` | I Visit `const ROOMS = [` → `{ slug: …, door:, q:, potion:` |
| `visit.full` | I Visit tick ` p.innerHTML = `` ` Something has gathered in the glass\` \`\` |
| `visit.form.*` | I `id="visitText"` placeholder; `>Bottle it<`; `id="visitOn">Leave it unwritten` |
| `visit.kept.*` | I Visit keep `line(text ? 'It goes on the shelf, still warm.' :` |
| `visit.end.1/2` | I Visit keep `line('Five small bottles`, `line('The palace keeps no hours` |
| `visit.end.shelf` | I `id="visitShelf" hidden>Go to the shelf` |
| `shelf.title/sub/back` | I `id="h-shelf">The shelf`, `<p class="sub">What the rooms gave you, in your own words`, `>Back to the palace<` |
| `shelf.empty` | I openShelf `'Nothing is bottled yet.` |
| `toast.glass.cleared*` | I clearGlass `mn.toast('Glass cleared', { action: 'Undo'` |
| `toast.sound.back*` | I primeSound `mn.toast('Sound is on', { action: 'Turn off'` |
| `toast.sound.key.*` | I onKey `toast(Sound.state.synth ? 'Sound on' : 'Sound off')` |
| `toast.listen.on` | I setListen `toast('Listening · play some music')` |
| `toast.drift.first` | I setWander ``toast(`A new view every`` |
| `toast.controls.hidden` | I setHidden `toast('Controls hidden` |
| `toast.cycle` | I cycle ``toast(`${SCHEMA[key][5]} · ${name(params[key])}`)`` |
| `whisper.breathe.*` | I breathCue `whisper(inhale ? 'Breathe in' : 'Breathe out'` |
| `toast.unavailable` | I buildLight and buildHome `toast(err.message \|\| "That isn't available here")` (2×) |
| `toast.hands.loading` | I `UI.toast('Loading fingertip tracking…')` |
| `toast.hands.ready` | I `UI.toast('Hands ready · raise a finger to paint')`; S `handsReady()` |
| `toast.hands.on` | I sideHands.onclick `toast('Raise a finger to paint` |
| `toast.hands.look*` | I sideHands.onclick ``mn.toast(`Hands: ${HAND_LOOKS`` `action: 'Change'` |
| `toast.hands.failed` | I `UI.toast("Fingertip tracking couldn't start` (2× in setHands worker); S `handsFailed()` and `setHands` throw |
| `toast.camera.disconnected` | I openStream `UI.toast('The camera was disconnected')` |
| `toast.picture.saved*` | I afterRender `mn.toast('Image saved to downloads', { action: 'Save in 4K'` |
| `toast.picture.rendering` | I exportHiRes ``toast(`Rendering ${w} × ${h}…`)`` |
| `toast.picture.hires.*` | I exportHiRes `` `${w} × ${h} image saved to downloads` ``, `'Not enough graphics memory` (2× in I, 1× in U patch 2) |
| `toast.film.started*` | I beginRecord `toast(audio ? 'Recording with sound · press R to stop'` (also U patch 4 anchor + replacement) |
| `toast.film.finishing` | I stopRecord `toast('Finishing the film…')` |
| `toast.film.saved*` | I offerShare `mn.toast('Video saved to downloads', { action: 'Share or save to Photos'`; r.onstop `toast(mp4 ? 'Video saved to downloads' :`; U patch 4 `saved:` |
| `toast.film.nobrowser` | I toggleRecord and beginRecord `"This browser can't record video` (2×; U patch 3 anchors the first) |
| `toast.film.dead` | I giveUp `'Recording stopped · this browser was not producing a film'` |
| `toast.film.slow` | I `'This device is encoding slowly` |
| `toast.film.cantkeepup` | I `'This device could not keep up with filming` |
| `toast.film.error` | I `r.onerror` `'Recording stopped · the browser reported an error'`; U patch 4 `failed:` |
| `toast.link.*` | I actions.copyLink `toast('Link copied · it opens this exact view')` (U patch 5 anchors it) |
| `toast.midi.connected` | I ``UI.toast(`MIDI connected ·`` |
| `toast.knob.learn` | I learn ``UI.toast(`Turn a knob to control`` |
| `toast.knob.assigned` | I onMidi ``UI.toast(`Knob ${d1} now controls`` |
| `toast.knobs.forgotten` | I buildAll forget `'Knob assignments forgotten` |
| `err.camera.https` | I openStream `'The camera needs HTTPS or localhost'` |
| `err.camera.open` | I setCamera `"That camera couldn't be opened"` |
| `err.camera.denied` | I getUserMedia rejection surfaces `err.message` (no app copy) |
| `err.listen.*` | I Sound.setMic `'Listening needs HTTPS or localhost'`, `'Listening needs microphone access` ; S setMic |
| `err.sound.none` | I Sound ensure `"This browser can't play sound"` |
| `err.midi.none` | I setMidi `'This browser has no MIDI support` |
| `err.tilt.*` | I setTilt `'This device has no tilt sensor'`, `'Tilt needs motion access`; S setTilt |
| `err.gpu.lost` | I `webglcontextlost` `UI.toast('GPU context lost` |
| `err.webgl` | I Renderer init `'WebGL2 is not available in this browser.'` → `#fatal` |
| `sr.canvas` | I `<canvas id="c" aria-label=` |
| `sr.orb.*` | I `<button class="orb" id="orb" aria-label="Controls"`; togglePanel `orb.setAttribute('aria-label', !next ? 'Controls' : … 'Back' : 'Close')` |
| `sr.halo`, `sr.ring.*` | I `<div class="halo" id="halo" role="dialog" aria-label="Controls">`, `id="swatches" … aria-label="Colour"`, `id="handsRing" … aria-label="Hands look"`, `id="calmRing" … aria-label="Calm"`; handLookRow `'aria-label': 'Hands look'` |
| `sr.brand` | I `id="brandTitle" aria-label="fractonaut 2026"` |
| `sr.welcome` | I showCaption `n.setAttribute('aria-label', 'Welcome, fractonaut')` |
| `sr.places.close`, `sr.settings.close` | I `<h2 id="h-places">…aria-label="Close"`; `panel-head` `data-close aria-label="Close"` |
| `sr.visit.flask` | I `id="flask" … aria-label="The glass filling"` |
| `sr.visit.input` | I `id="visitText"` `aria-label="Write what came to you"` |
| `sr.swatch` | I buildMood `'aria-label': cap(p), title: cap(p)` |
| `native.mac.name` / `.window` | P `CFBundleName`, `CFBundleDisplayName`; MM `_window.title = @"fractonaut"`, `NSString* name = @"fractonaut"` |
| `native.android.name*` | G `manifestPlaceholders["appLabel"] = if (dev) "fractonaut dev" else "fractonaut"` |
| `native.android.folder` | ME `DIRECTORY_MOVIES}/fractonaut`, `DIRECTORY_PICTURES}/fractonaut` |
| `native.mac.perm.*` | P `NSCameraUsageDescription`, `NSMicrophoneUsageDescription` |
| `native.toast.camera.stopped` | S `cameraFailed()` `UI.toast('The camera stopped')` |
| `native.err.camera` | S Inputs.setCam `throw new Error("That isn't available here")` |
| `native.camera.*` | V `LENS_FACING_FRONT -> "Front camera"` … `"$kind $n"`; S `c.label \|\| 'Camera'`; mac/app/web.mm `?: @"Camera"` |
| `native.mac.menu.*` | MM buildMenu `item(@"Hide Others"` … `@"Enter Full Screen"` |
| `dev.handrate` | I ``el.textContent = `hands ${`` |
| `dev.shader` / `dev.link` | I `'Shader compile failed: '`, `'Link failed: '` |