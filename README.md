# openvice
Open source engine for [Grand Theft Auto: Vice City](https://www.rockstargames.com/games/vicecity).

Features:
* Read GTA Vice City format files: IMG, DIR, DFF, TXD
* Render city

## Build
1. Download and build dependencies
2. Set up a project Microsoft Visual Studio 2019 using dependencies
3. Open `openvice.sln` solution in Microsoft Visual Studio 2019 and click `Build` -> `Build Solution`

### Build OpenSceneGraph
1. Set the following variables ON:
```
OSG_GL3_AVAILABLE
```
   
2. Set the following variables OFF:
```
OSG_GL1_AVAILABLE
OSG_GL2_AVAILABLE
OSG_GLES1_AVAILABLE
OSG_GLES2_AVAILABLE
OSG_GL_DISPLAYLISTS_AVAILABLE
OSG_GL_FIXED_FUNCTION_AVAILABLE
OSG_GL_MATRICES_AVAILABLE
OSG_GL_VERTEX_ARRAY_FUNCS_AVAILABLE
OSG_GL_VERTEX_FUNCS_AVAILABLE
```

3. Set the following variables:
```
OSG_CONTEXT_VERSION to 3.3
```

## System requirements
* Windows Vista or higher
* OpenGL 3.3

## Dependencies
* [OpenSceneGraph](https://github.com/openscenegraph/OpenSceneGraph)

## Tested
* Windows 10 Pro (version 22H2)
