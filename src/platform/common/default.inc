R"TSRB(
configVersion = 2
defaultMode = 'Fullscreen'
guiFontPath = ''
guiFontSize = 20
guiHeight = 818
guiHotkey = [ 162, 73 ]
guiOpacity = 1.0
guiScale = 1.25
guiTheme = 'Monokai'
guiWidth = 962
keyRepeatAffectsMouseButtons = false
keyRepeatDelay = 0
keyRepeatStartDelay = 0
mouseSensitivity = 1.0
rebindToggleHotkey = []
windowsMouseSpeed = 0

[eyezoom]
autoFontSize = true
centerLineColor = [ 255, 255, 255 ]
centerLineColorOpacity = 1.0
cloneHeight = 1300
cloneWidth = 30
gridColor1 = [ 255, 182, 193 ]
gridColor1Opacity = 1.0
gridColor2 = [ 173, 216, 230 ]
gridColor2Opacity = 1.0
horizontalMargin = 0
linkRectToFont = true
outputAspectFitMode = 'contain'
outputHeight = 900
outputPreserveAspectRatio = true
outputRelativeHeight = 0.65170168876647949
outputRelativeTo = 'middleLeftScreen'
outputRelativeWidth = 0.3125
outputRelativeX = 0.05000000074505806
outputRelativeY = 0.0
outputUseRelativePosition = true
outputUseRelativeSize = true
outputX = 0.05000000074505806
outputY = 0.0
overlayWidth = 12
positionX = 0
positionY = 0
rectHeight = 50
slideMirrorsIn = true
slideZoomIn = true
stretchWidth = 800
textColor = [ 0, 0, 0 ]
textColorOpacity = 1.0
textFontPath = ''
textFontSize = 42
useCustomSizePosition = true
verticalMargin = 0
windowHeight = 16384
windowWidth = 384
zoomAreaHeight = 900
zoomAreaWidth = 800

[keyRebinds]
enabled = false

[[hotkey]]
altSecondaryModes = []
debounce = 100
keys = [ 90 ]
mainMode = 'Fullscreen'
secondaryMode = 'Thin'
triggerOnHold = false
triggerOnRelease = false

    [hotkey.conditions]
    exclusions = [ 114 ]
    gameState = []

[[hotkey]]
altSecondaryModes = []
debounce = 100
keys = [ 74 ]
mainMode = 'Fullscreen'
secondaryMode = 'EyeZoom'
triggerOnHold = false
triggerOnRelease = false

    [hotkey.conditions]
    exclusions = [ 114 ]
    gameState = []

[[hotkey]]
altSecondaryModes = []
debounce = 100
keys = [ 164 ]
mainMode = 'Fullscreen'
secondaryMode = 'Wide'
triggerOnHold = false
triggerOnRelease = false

    [hotkey.conditions]
    exclusions = [ 114 ]
    gameState = []

[[mirror]]
captureHeight = 7
captureWidth = 23
colorPassthrough = false
colorSensitivity = 0.001
colors = { border = [ 0, 0, 0 ], output = [ 255, 255, 255 ], targetColors = [ [ 221, 221, 221 ], [ 67, 67, 67 ] ] }
fps = 100
input = [ { relativeTo = 'topLeftViewport', x = 14, y = 38 } ]
name = 'E Counter'
onlyOnMyScreen = false
opacity = 1.0
output = { aspectFitMode = 'contain', preserveAspectRatio = true, relativeHeight = 0.5, relativeTo = 'topRightViewport', relativeWidth = 0.5, relativeX = 1.1000000238418579, relativeY = 0.079999998211860657, scale = 8.0, scaleX = 1.0, scaleY = 1.0, separateScale = false, useRelativePosition = true, useRelativeSize = false, x = 1.1000000238418579, y = 0.079999998211860657 }
rawOutput = false

    [mirror.border]
    dynamicThickness = 2
    staticColor = [ 255, 255, 255 ]
    staticHeight = 0
    staticOffsetX = 0
    staticOffsetY = 0
    staticRadius = 0
    staticShape = 'Rectangle'
    staticThickness = 2
    staticWidth = 0
    type = 'Dynamic'

[[mirror]]
captureHeight = 7
captureWidth = 19
colorPassthrough = false
colorSensitivity = 0.001
colors = { border = [ 0, 0, 0 ], output = [ 255, 255, 255 ], targetColors = [ [ 233, 109, 77 ] ] }
fps = 100
input = [
    { relativeTo = 'pieRight', x = 0, y = 0 },
    { relativeTo = 'pieRight', x = 0, y = 8 },
    { relativeTo = 'pieRight', x = 0, y = 16 },
    { relativeTo = 'pieRight', x = 0, y = 24 }
]
name = 'Mapless'
onlyOnMyScreen = false
opacity = 1.0
output = { relativeTo = 'bottomRightViewport', relativeX = 0.5, relativeY = 0.5, scale = 8.0, scaleX = 8.0, scaleY = 8.0, separateScale = false, useRelativePosition = false, x = 71, y = 242 }
rawOutput = false

    [mirror.border]
    dynamicThickness = 2
    staticColor = [ 255, 255, 255 ]
    staticHeight = 0
    staticOffsetX = 0
    staticOffsetY = 0
    staticRadius = 0
    staticShape = 'Rectangle'
    staticThickness = 2
    staticWidth = 0
    type = 'Dynamic'

[[mirror]]
captureHeight = 7
captureWidth = 26
colorPassthrough = true
colorSensitivity = 0.0010000000474974513
colors = { border = [ 19, 56, 50 ], output = [ 243, 169, 78 ], targetColors = [ [ 77, 225, 202 ] ] }
fps = 100
input = [
    { relativeTo = 'pieLeft', x = 0, y = 0 },
    { relativeTo = 'pieLeft', x = 0, y = 8 },
    { relativeTo = 'pieLeft', x = 0, y = 16 },
    { relativeTo = 'pieLeft', x = 0, y = 24 }
]
name = 'minecraft:mob_spawner left'
onlyOnMyScreen = false
opacity = 1.0
output = { aspectFitMode = 'contain', preserveAspectRatio = true, relativeHeight = 0.056880518794059753, relativeTo = 'bottomRightViewport', relativeWidth = 0.095462493598461151, relativeX = 0.0, relativeY = 0.004999999888241291, scale = 6.0, scaleX = 1.0, scaleY = 1.0, separateScale = false, useRelativePosition = true, useRelativeSize = true, x = 0.0, y = 0.004999999888241291 }
rawOutput = false

    [mirror.border]
    dynamicThickness = 1
    staticColor = [ 255, 255, 255 ]
    staticHeight = 0
    staticOffsetX = 0
    staticOffsetY = 0
    staticRadius = 0
    staticShape = 'Rectangle'
    staticThickness = 0
    staticWidth = 0
    type = 'Dynamic'

[[mirror]]
captureHeight = 7
captureWidth = 11
colorPassthrough = false
colorSensitivity = 0.001
colors = { border = [ 90, 54, 14 ], output = [ 243, 169, 78 ], targetColors = [ [ 233, 109, 77 ] ] }
fps = 100
input = [
    { relativeTo = 'pieLeft', x = 0, y = 0 },
    { relativeTo = 'pieLeft', x = 0, y = 8 },
    { relativeTo = 'pieLeft', x = 0, y = 16 },
    { relativeTo = 'pieLeft', x = 0, y = 24 }
]
name = 'blockentities left'
onlyOnMyScreen = false
opacity = 1.0
output = { relativeTo = 'centerScreen', relativeX = 0.5, relativeY = 0.5, scale = 8.0, scaleX = 1.0, scaleY = 1.0, separateScale = false, useRelativePosition = false, x = 0, y = 0 }
rawOutput = false

    [mirror.border]
    dynamicThickness = 2
    staticColor = [ 255, 255, 255 ]
    staticHeight = 0
    staticOffsetX = 0
    staticOffsetY = 0
    staticRadius = 0
    staticShape = 'Rectangle'
    staticThickness = 2
    staticWidth = 0
    type = 'Dynamic'

[[mirror]]
captureHeight = 7
captureWidth = 11
colorPassthrough = true
colorSensitivity = 0.001
colors = { border = [ 51, 88, 48 ], output = [ 0, 0, 0 ], targetColors = [ [ 69, 204, 101 ], [ 69, 203, 101 ] ] }
fps = 100
input = [
    { relativeTo = 'pieLeft', x = 0, y = 0 },
    { relativeTo = 'pieLeft', x = 0, y = 8 },
    { relativeTo = 'pieLeft', x = 0, y = 16 },
    { relativeTo = 'pieLeft', x = 0, y = 24 }
]
name = 'unspecified left'
onlyOnMyScreen = false
opacity = 1.0
output = { relativeTo = 'centerViewport', relativeX = 0.5, relativeY = 0.5, scale = 8.0, scaleX = 1.0, scaleY = 1.0, separateScale = false, useRelativePosition = false, x = 0, y = 0 }
rawOutput = false

    [mirror.border]
    dynamicThickness = 2
    staticColor = [ 255, 255, 255 ]
    staticHeight = 0
    staticOffsetX = 0
    staticOffsetY = 0
    staticRadius = 0
    staticShape = 'Rectangle'
    staticThickness = 2
    staticWidth = 0
    type = 'Dynamic'

[[mirror]]
captureHeight = 169
captureWidth = 319
colorPassthrough = true
colorSensitivity = 0.001
colors = { border = [ 35, 30, 37 ], output = [ 239, 187, 187 ], targetColors = [ [ 70, 206, 102 ], [ 236, 110, 78 ], [ 228, 70, 196 ], [ 204, 108, 70 ], [ 70, 76, 70 ] ] }
fps = 100
input = [ { relativeTo = 'pieLeft', x = -238, y = -180 } ]
name = 'Pie'
onlyOnMyScreen = false
opacity = 1.0
output = { aspectFitMode = 'contain', preserveAspectRatio = true, relativeHeight = 0.0099999997764825821, relativeTo = 'centerViewport', relativeWidth = 0.64322918653488159, relativeX = 0.8913043737411499, relativeY = 0.02152777835726738, scale = 1.1399999856948853, scaleX = 0.75999999046325684, scaleY = 1.5, separateScale = true, useRelativePosition = false, useRelativeSize = true, x = 410, y = 31 }
rawOutput = false

    [mirror.border]
    dynamicThickness = 3
    staticColor = [ 98, 5, 113 ]
    staticHeight = 235
    staticOffsetX = 1
    staticOffsetY = -7
    staticRadius = 171
    staticShape = 'Circle'
    staticThickness = 3
    staticWidth = 238
    type = 'Dynamic'

[[mirror]]
captureHeight = 8
captureWidth = 14
colorPassthrough = true
colorSensitivity = 0.00030000001424923539
fps = 100
name = 'tick key'
onlyOnMyScreen = false
opacity = 1.0
rawOutput = false

    [mirror.border]
    dynamicThickness = 1
    staticColor = [ 255, 255, 255 ]
    staticHeight = 0
    staticOffsetX = 0
    staticOffsetY = 0
    staticRadius = 0
    staticShape = 'Rectangle'
    staticThickness = 0
    staticWidth = 0
    type = 'Dynamic'

    [mirror.colors]
    border = [ 0, 0, 0 ]
    output = [ 0, 0, 0 ]
    targetColors = [ [ 101, 67, 202 ], [ 25, 17, 50 ] ]

    [mirror.output]
    aspectFitMode = 'contain'
    preserveAspectRatio = true
    relativeHeight = 0.5
    relativeTo = 'centerScreen'
    relativeWidth = 0.5
    relativeX = -0.1119999960064888
    relativeY = -0.068999998271465302
    scale = 1.0
    scaleX = 1.0
    scaleY = 1.0
    separateScale = false
    useRelativePosition = false
    useRelativeSize = false
    x = 0
    y = 0

    [[mirror.input]]
    enabled = false
    relativeTo = 'pieLeft'
    x = -238
    y = 0

    [[mirror.input]]
    enabled = true
    relativeTo = 'pieLeft'
    x = -238
    y = 8

    [[mirror.input]]
    enabled = true
    relativeTo = 'pieLeft'
    x = -238
    y = 16

    [[mirror.input]]
    enabled = true
    relativeTo = 'pieLeft'
    x = -238
    y = 24

    [[mirror.input]]
    enabled = true
    relativeTo = 'pieLeft'
    x = -238
    y = 32

    [[mirror.input]]
    enabled = true
    relativeTo = 'pieLeft'
    x = -238
    y = 40

    [[mirror.input]]
    enabled = true
    relativeTo = 'pieLeft'
    x = -238
    y = 48

    [[mirror.input]]
    enabled = true
    relativeTo = 'pieLeft'
    x = -238
    y = 56

    [[mirror.input]]
    enabled = true
    relativeTo = 'pieLeft'
    x = -238
    y = 64

[[mirror]]
captureHeight = 8
captureWidth = 14
colorPassthrough = true
colorSensitivity = 0.0032999999821186066
fps = 100
name = 'level key'
onlyOnMyScreen = false
opacity = 1.0
rawOutput = false

    [mirror.border]
    dynamicThickness = 1
    staticColor = [ 255, 255, 255 ]
    staticHeight = 0
    staticOffsetX = 0
    staticOffsetY = 0
    staticRadius = 0
    staticShape = 'Rectangle'
    staticThickness = 0
    staticWidth = 0
    type = 'Dynamic'

    [mirror.colors]
    border = [ 0, 0, 0 ]
    output = [ 0, 0, 0 ]
    targetColors = [ [ 100, 204, 194 ], [ 25, 50, 48 ] ]

    [mirror.output]
    aspectFitMode = 'contain'
    preserveAspectRatio = true
    relativeHeight = 0.5
    relativeTo = 'centerScreen'
    relativeWidth = 0.5
    relativeX = 0.12812499701976776
    relativeY = -0.068790733814239502
    scale = 1.0
    scaleX = 1.0
    scaleY = 1.0
    separateScale = false
    useRelativePosition = false
    useRelativeSize = false
    x = 0
    y = 0

    [[mirror.input]]
    enabled = true
    relativeTo = 'pieLeft'
    x = -238
    y = 0

    [[mirror.input]]
    enabled = false
    relativeTo = 'pieLeft'
    x = -238
    y = 8

    [[mirror.input]]
    enabled = false
    relativeTo = 'pieLeft'
    x = -238
    y = 16

    [[mirror.input]]
    enabled = false
    relativeTo = 'pieLeft'
    x = -238
    y = 24

    [[mirror.input]]
    enabled = false
    relativeTo = 'pieLeft'
    x = -238
    y = 32

    [[mirror.input]]
    enabled = false
    relativeTo = 'pieLeft'
    x = -238
    y = 40

    [[mirror.input]]
    enabled = false
    relativeTo = 'pieLeft'
    x = -238
    y = 48

    [[mirror.input]]
    enabled = false
    relativeTo = 'pieLeft'
    x = -238
    y = 56

    [[mirror.input]]
    enabled = false
    relativeTo = 'pieLeft'
    x = -238
    y = 64

[[mirror]]
captureHeight = 8
captureWidth = 14
colorPassthrough = true
colorSensitivity = 0.00030000001424923539
fps = 100
name = 'entities key'
onlyOnMyScreen = false
opacity = 1.0
rawOutput = false

    [mirror.border]
    dynamicThickness = 1
    staticColor = [ 255, 255, 255 ]
    staticHeight = 0
    staticOffsetX = 0
    staticOffsetY = 0
    staticRadius = 0
    staticShape = 'Rectangle'
    staticThickness = 0
    staticWidth = 0
    type = 'Dynamic'

    [mirror.colors]
    border = [ 0, 0, 0 ]
    output = [ 0, 0, 0 ]
    targetColors = [ [ 225, 69, 194 ], [ 56, 17, 48 ] ]

    [mirror.output]
    aspectFitMode = 'contain'
    preserveAspectRatio = true
    relativeHeight = 0.5
    relativeTo = 'centerScreen'
    relativeWidth = 0.5
    relativeX = 0.12812499701976776
    relativeY = -0.068790733814239502
    scale = 1.0
    scaleX = 1.0
    scaleY = 1.0
    separateScale = false
    useRelativePosition = false
    useRelativeSize = false
    x = 0
    y = 0

    [[mirror.input]]
    enabled = true
    relativeTo = 'pieLeft'
    x = -238
    y = 0

    [[mirror.input]]
    enabled = true
    relativeTo = 'pieLeft'
    x = -238
    y = 8

    [[mirror.input]]
    enabled = false
    relativeTo = 'pieLeft'
    x = -238
    y = 16

    [[mirror.input]]
    enabled = false
    relativeTo = 'pieLeft'
    x = -238
    y = 24

    [[mirror.input]]
    enabled = false
    relativeTo = 'pieLeft'
    x = -238
    y = 32

    [[mirror.input]]
    enabled = false
    relativeTo = 'pieLeft'
    x = -238
    y = 40

    [[mirror.input]]
    enabled = false
    relativeTo = 'pieLeft'
    x = -238
    y = 48

    [[mirror.input]]
    enabled = false
    relativeTo = 'pieLeft'
    x = -238
    y = 56

    [[mirror.input]]
    enabled = false
    relativeTo = 'pieLeft'
    x = -238
    y = 64

[[mirror]]
captureHeight = 8
captureWidth = 14
colorPassthrough = true
colorSensitivity = 0.00030000001424923539
fps = 100
name = 'blockEntities key'
onlyOnMyScreen = false
opacity = 1.0
rawOutput = false

    [mirror.border]
    dynamicThickness = 1
    staticColor = [ 255, 255, 255 ]
    staticHeight = 0
    staticOffsetX = 0
    staticOffsetY = 0
    staticRadius = 0
    staticShape = 'Rectangle'
    staticThickness = 0
    staticWidth = 0
    type = 'Dynamic'

    [mirror.colors]
    border = [ 0, 0, 0 ]
    output = [ 0, 0, 0 ]
    targetColors = [ [ 196, 196, 109 ], [ 48, 48, 27 ] ]

    [mirror.output]
    aspectFitMode = 'contain'
    preserveAspectRatio = true
    relativeHeight = 0.0099999997764825821
    relativeTo = 'centerScreen'
    relativeWidth = 0.0099999997764825821
    relativeX = 0.12812499701976776
    relativeY = -0.068790733814239502
    scale = 1.0
    scaleX = 1.0
    scaleY = 1.0
    separateScale = false
    useRelativePosition = false
    useRelativeSize = false
    x = 0
    y = 0

    [[mirror.input]]
    enabled = true
    relativeTo = 'pieLeft'
    x = -238
    y = 0

    [[mirror.input]]
    enabled = true
    relativeTo = 'pieLeft'
    x = -238
    y = 8

    [[mirror.input]]
    enabled = true
    relativeTo = 'pieLeft'
    x = -238
    y = 16

    [[mirror.input]]
    enabled = true
    relativeTo = 'pieLeft'
    x = -238
    y = 24

    [[mirror.input]]
    enabled = true
    relativeTo = 'pieLeft'
    x = -238
    y = 32

    [[mirror.input]]
    enabled = true
    relativeTo = 'pieLeft'
    x = -238
    y = 40

    [[mirror.input]]
    enabled = true
    relativeTo = 'pieLeft'
    x = -238
    y = 48

    [[mirror.input]]
    enabled = true
    relativeTo = 'pieLeft'
    x = -238
    y = 56

    [[mirror.input]]
    enabled = true
    relativeTo = 'pieLeft'
    x = -238
    y = 64

[[mirror]]
captureHeight = 8
captureWidth = 14
colorPassthrough = true
colorSensitivity = 0.00030000001424923539
fps = 100
name = 'gameRenderer key'
onlyOnMyScreen = false
opacity = 1.0
rawOutput = false

    [mirror.border]
    dynamicThickness = 1
    staticColor = [ 255, 255, 255 ]
    staticHeight = 0
    staticOffsetX = 0
    staticOffsetY = 0
    staticRadius = 0
    staticShape = 'Rectangle'
    staticThickness = 0
    staticWidth = 0
    type = 'Dynamic'

    [mirror.colors]
    border = [ 0, 0, 0 ]
    output = [ 0, 0, 0 ]
    targetColors = [ [ 194, 204, 194 ], [ 48, 50, 48 ] ]

    [mirror.output]
    aspectFitMode = 'contain'
    preserveAspectRatio = true
    relativeHeight = 0.5
    relativeTo = 'centerScreen'
    relativeWidth = 0.5
    relativeX = 0.12812499701976776
    relativeY = -0.068790733814239502
    scale = 1.0
    scaleX = 1.0
    scaleY = 1.0
    separateScale = false
    useRelativePosition = false
    useRelativeSize = false
    x = 0
    y = 0

    [[mirror.input]]
    enabled = true
    relativeTo = 'pieLeft'
    x = -238
    y = 0

    [[mirror.input]]
    enabled = true
    relativeTo = 'pieLeft'
    x = -238
    y = 8

    [[mirror.input]]
    enabled = true
    relativeTo = 'pieLeft'
    x = -238
    y = 16

    [[mirror.input]]
    enabled = false
    relativeTo = 'pieLeft'
    x = -238
    y = 24

    [[mirror.input]]
    enabled = false
    relativeTo = 'pieLeft'
    x = -238
    y = 32

    [[mirror.input]]
    enabled = false
    relativeTo = 'pieLeft'
    x = -238
    y = 40

    [[mirror.input]]
    enabled = false
    relativeTo = 'pieLeft'
    x = -238
    y = 48

    [[mirror.input]]
    enabled = false
    relativeTo = 'pieLeft'
    x = -238
    y = 56

    [[mirror.input]]
    enabled = false
    relativeTo = 'pieLeft'
    x = -238
    y = 64

[[mirrorGroup]]
mirrors = [
    { enabled = true, heightPercent = 1.4700000286102295, mirrorId = 'Pie', offsetX = 360, offsetY = 0, widthPercent = 1.5900000333786011 },
    { enabled = true, heightPercent = 1.0, mirrorId = 'blockentities left', offsetX = 590, offsetY = -46, widthPercent = 1.0 },
    { enabled = true, heightPercent = 1.0, mirrorId = 'unspecified left', offsetX = 590, offsetY = 26, widthPercent = 1.0 }
]
name = 'Preemptive Pie'
output = { aspectFitMode = 'fitHeight', preserveAspectRatio = true, relativeHeight = 0.18437500298023224, relativeTo = 'middleLeftScreen', relativeWidth = 0.54026085138320923, relativeX = 0.48900002241134644, relativeY = 0.0, scale = 1.0, scaleX = 1.0, scaleY = 1.0, separateScale = false, useRelativePosition = true, useRelativeSize = false, x = 0.48900002241134644, y = 0.0 }

[[mirrorGroup]]
name = 'root.tick.level.entities.blockEntities'

    [mirrorGroup.output]
    aspectFitMode = 'contain'
    preserveAspectRatio = true
    relativeHeight = 0.0486111119389534
    relativeTo = 'bottomRightViewport'
    relativeWidth = 0.04374999925494194
    relativeX = 0.004999999888241291
    relativeY = 0.004999999888241291
    scale = 5.5
    scaleX = 6.1279997825622559
    scaleY = 6.1279997825622559
    separateScale = false
    useRelativePosition = true
    useRelativeSize = true
    x = 0.004999999888241291
    y = 0.004999999888241291

    [[mirrorGroup.mirrors]]
    enabled = true
    heightPercent = 1.0
    mirrorId = 'tick key'
    offsetX = 0
    offsetY = 0
    widthPercent = 1.0

    [[mirrorGroup.mirrors]]
    enabled = true
    heightPercent = 1.0
    mirrorId = 'level key'
    offsetX = 0
    offsetY = 0
    widthPercent = 1.0

    [[mirrorGroup.mirrors]]
    enabled = true
    heightPercent = 1.0
    mirrorId = 'entities key'
    offsetX = 0
    offsetY = 0
    widthPercent = 1.0

    [[mirrorGroup.mirrors]]
    enabled = true
    heightPercent = 1.0
    mirrorId = 'blockEntities key'
    offsetX = 0
    offsetY = 0
    widthPercent = 1.0

[[mirrorGroup]]
name = 'root.gameRenderer.level'

    [mirrorGroup.output]
    aspectFitMode = 'contain'
    preserveAspectRatio = true
    relativeHeight = 0.0486111119389534
    relativeTo = 'bottomRightViewport'
    relativeWidth = 0.04374999925494194
    relativeX = 0.051999997347593307
    relativeY = 0.004999999888241291
    scale = 5.5
    scaleX = 6.1279997825622559
    scaleY = 6.1279997825622559
    separateScale = false
    useRelativePosition = true
    useRelativeSize = true
    x = 0.051999997347593307
    y = 0.004999999888241291

    [[mirrorGroup.mirrors]]
    enabled = true
    heightPercent = 1.0
    mirrorId = 'entities key'
    offsetX = 0
    offsetY = 0
    widthPercent = 1.0

    [[mirrorGroup.mirrors]]
    enabled = true
    heightPercent = 1.0
    mirrorId = 'gameRenderer key'
    offsetX = 0
    offsetY = 0
    widthPercent = 1.0

    [[mirrorGroup.mirrors]]
    enabled = true
    heightPercent = 1.0
    mirrorId = 'level key'
    offsetX = 0
    offsetY = 0
    widthPercent = 1.0

[[mode]]
border = { color = [ 255, 255, 255 ], enabled = false, radius = 0, width = 4 }
height = 1.0
id = 'Fullscreen'
imageIds = []
mirrorGroupIds = [ 'root.tick.level.entities.blockEntities', 'root.gameRenderer.level' ]
mirrorIds = [ 'Mapless', 'minecraft:mob_spawner left' ]
modeSensitivity = 1.0
modeSensitivityX = 1.0
modeSensitivityY = 1.0
positionPreset = 'topLeftScreen'
relativeHeight = 1.0
relativeWidth = 1.0
sensitivityOverrideEnabled = false
separateXYSensitivity = false
stretch = { enabled = true, height = 1440, width = 2560, x = 0, y = 0 }
useRelativeSize = true
width = 1.0
windowOverlayIds = []
x = 0
y = 0

    [mode.transition]
    gameTransition = 'Bounce'
    overlayTransition = 'Cut'
    backgroundTransition = 'Cut'
    transitionDurationMs = 300
    easeInPower = 1.0
    easeOutPower = 3.0
    bounceCount = 0
    bounceIntensity = 0.15
    bounceDurationMs = 150
    relativeStretching = false
    skipAnimateX = false
    skipAnimateY = false
    slideMirrorsIn = true

    [mode.background]
    color = [ 0, 0, 0 ]
    gradientAngle = 62.599998474121094
    gradientAnimation = 'Wave'
    gradientAnimationSpeed = 0.43999999761581421
    gradientColorFade = false
    image = ''
    selectedMode = 'gradient'

        [[mode.background.gradientStops]]
        color = [ 0, 0, 0 ]
        position = 0.008999999612569809

        [[mode.background.gradientStops]]
        color = [ 10, 10, 10 ]
        position = 0.018999999389052391

        [[mode.background.gradientStops]]
        color = [ 35, 30, 35 ]
        position = 0.019999999552965164

        [[mode.background.gradientStops]]
        color = [ 0, 0, 0 ]
        position = 0.029999999329447746

[[mode]]
border = { color = [ 35, 30, 37 ], enabled = true, radius = 0, width = 2 }
height = 16384
id = 'EyeZoom'
imageIds = []
mirrorGroupIds = [ 'Preemptive Pie' ]
mirrorIds = [ 'E Counter' ]
modeSensitivity = 1.0
modeSensitivityX = 1.0
modeSensitivityY = 1.0
positionPreset = 'centerScreen'
sensitivityOverrideEnabled = false
separateXYSensitivity = false
stretch = { enabled = false, height = 0, width = 0, x = 0, y = 0 }
width = 384
windowOverlayIds = []
x = 0
y = 0

    [mode.transition]
    gameTransition = 'Bounce'
    overlayTransition = 'Cut'
    backgroundTransition = 'Cut'
    transitionDurationMs = 300
    easeInPower = 1.0
    easeOutPower = 3.0
    bounceCount = 0
    bounceIntensity = 0.15
    bounceDurationMs = 150
    relativeStretching = false
    skipAnimateX = false
    skipAnimateY = true
    slideMirrorsIn = true

    [mode.background]
    color = [ 0, 0, 0 ]
    gradientAngle = 62.599998474121094
    gradientAnimation = 'Wave'
    gradientAnimationSpeed = 0.43999999761581421
    gradientColorFade = false
    image = ''
    selectedMode = 'gradient'

        [[mode.background.gradientStops]]
        color = [ 0, 0, 0 ]
        position = 0.008999999612569809

        [[mode.background.gradientStops]]
        color = [ 10, 10, 10 ]
        position = 0.018999999389052391

        [[mode.background.gradientStops]]
        color = [ 35, 30, 35 ]
        position = 0.019999999552965164

        [[mode.background.gradientStops]]
        color = [ 0, 0, 0 ]
        position = 0.029999999329447746

[[mode]]
border = { color = [ 35, 30, 37 ], enabled = true, radius = 0, width = 2 }
height = 16384
id = 'Preemptive'
imageIds = []
mirrorGroupIds = [ 'Preemptive Pie' ]
mirrorIds = [ 'E Counter' ]
modeSensitivity = 1.0
modeSensitivityX = 1.0
modeSensitivityY = 1.0
positionPreset = 'centerScreen'
sensitivityOverrideEnabled = false
separateXYSensitivity = false
stretch = { enabled = false, height = 0, width = 0, x = 0, y = 0 }
width = 384
windowOverlayIds = []
x = 0
y = 0

    [mode.transition]
    gameTransition = 'Bounce'
    overlayTransition = 'Cut'
    backgroundTransition = 'Cut'
    transitionDurationMs = 300
    easeInPower = 1.0
    easeOutPower = 3.0
    bounceCount = 0
    bounceIntensity = 0.15
    bounceDurationMs = 150
    relativeStretching = false
    skipAnimateX = false
    skipAnimateY = true
    slideMirrorsIn = true

    [mode.background]
    color = [ 0, 0, 0 ]
    gradientAngle = 62.599998474121094
    gradientAnimation = 'Wave'
    gradientAnimationSpeed = 0.43999999761581421
    gradientColorFade = false
    image = ''
    selectedMode = 'gradient'

        [[mode.background.gradientStops]]
        color = [ 0, 0, 0 ]
        position = 0.008999999612569809

        [[mode.background.gradientStops]]
        color = [ 10, 10, 10 ]
        position = 0.018999999389052391

        [[mode.background.gradientStops]]
        color = [ 35, 30, 35 ]
        position = 0.019999999552965164

        [[mode.background.gradientStops]]
        color = [ 0, 0, 0 ]
        position = 0.029999999329447746

[[mode]]
border = { color = [ 35, 30, 37 ], enabled = true, radius = 0, width = 2 }
height = 1.0
id = 'Thin'
imageIds = []
mirrorGroupIds = [ 'Preemptive Pie' ]
mirrorIds = [ 'Mapless', 'E Counter' ]
modeSensitivity = 1.0
modeSensitivityX = 1.0
modeSensitivityY = 1.0
positionPreset = 'centerScreen'
relativeHeight = 1.0
relativeWidth = 0.18000000715255737
sensitivityOverrideEnabled = false
separateXYSensitivity = false
stretch = { enabled = false, height = 0, width = 0, x = 0, y = 0 }
useRelativeSize = true
width = 0.18000000715255737
windowOverlayIds = []
x = 0
y = 0

    [mode.transition]
    gameTransition = 'Bounce'
    overlayTransition = 'Cut'
    backgroundTransition = 'Cut'
    transitionDurationMs = 300
    easeInPower = 1.0
    easeOutPower = 3.0
    bounceCount = 0
    bounceIntensity = 0.02
    bounceDurationMs = 200
    relativeStretching = false
    skipAnimateX = false
    skipAnimateY = false
    slideMirrorsIn = true

    [mode.background]
    color = [ 0, 0, 0 ]
    gradientAngle = 62.599998474121094
    gradientAnimation = 'Wave'
    gradientAnimationSpeed = 0.43999999761581421
    gradientColorFade = false
    image = ''
    selectedMode = 'gradient'

        [[mode.background.gradientStops]]
        color = [ 0, 0, 0 ]
        position = 0.008999999612569809

        [[mode.background.gradientStops]]
        color = [ 10, 10, 10 ]
        position = 0.018999999389052391

        [[mode.background.gradientStops]]
        color = [ 35, 30, 35 ]
        position = 0.019999999552965164

        [[mode.background.gradientStops]]
        color = [ 0, 0, 0 ]
        position = 0.029999999329447746

[[mode]]
border = { color = [ 35, 30, 37 ], enabled = true, radius = 0, width = 2 }
height = 0.25
id = 'Wide'
imageIds = []
mirrorGroupIds = []
mirrorIds = []
modeSensitivity = 1.0
modeSensitivityX = 1.0
modeSensitivityY = 1.0
positionPreset = 'centerScreen'
relativeHeight = 0.25
relativeWidth = 1.0
sensitivityOverrideEnabled = false
separateXYSensitivity = false
stretch = { enabled = false, height = 0, width = 0, x = 0, y = 0 }
useRelativeSize = true
width = 1.0
windowOverlayIds = []
x = 0
y = 0

    [mode.transition]
    gameTransition = 'Bounce'
    overlayTransition = 'Cut'
    backgroundTransition = 'Cut'
    transitionDurationMs = 300
    easeInPower = 1.0
    easeOutPower = 3.0
    bounceCount = 0
    bounceIntensity = 0.15
    bounceDurationMs = 150
    relativeStretching = false
    skipAnimateX = false
    skipAnimateY = false
    slideMirrorsIn = true

    [mode.background]
    color = [ 0, 0, 0 ]
    gradientAngle = 62.599998474121094
    gradientAnimation = 'Wave'
    gradientAnimationSpeed = 0.43999999761581421
    gradientColorFade = false
    image = ''
    selectedMode = 'gradient'

        [[mode.background.gradientStops]]
        color = [ 0, 0, 0 ]
        position = 0.008999999612569809

        [[mode.background.gradientStops]]
        color = [ 10, 10, 10 ]
        position = 0.018999999389052391

        [[mode.background.gradientStops]]
        color = [ 35, 30, 35 ]
        position = 0.019999999552965164

        [[mode.background.gradientStops]]
        color = [ 0, 0, 0 ]
        position = 0.029999999329447746
)TSRB"
