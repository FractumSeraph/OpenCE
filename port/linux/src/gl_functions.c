/*
GL_FUNCTIONS.C

Run-time resolution of the OpenGL entry points listed in gl.h.
*/

#include "platform.h"
#define GL_FUNCTIONS_DEFINE
#include "gl.h"

#include <SDL3/SDL.h>
#include <string.h>

#ifdef HALO_BROWSER

int gl_functions_load(void)
{
	/* Emscripten exposes the WebGL 2 entry points directly. */
	return TRUE;
}

#else

#define GL_DEFINE_FUNCTION(name) __typeof__(&name) halo_##name;
GL_FUNCTIONS(GL_DEFINE_FUNCTION)

int gl_functions_load(void)
{
	int success = TRUE;

/* newer than OpenGL 4.1 (macOS's newest); callers check for NULL */
#define GL_OPTIONAL_FUNCTION(name) \
	(!strcmp(#name, "glClipControl") || !strcmp(#name, "glCopyImageSubData") || \
		!strcmp(#name, "glDebugMessageCallback") || !strcmp(#name, "glBufferStorage") || \
		!strcmp(#name, "glMemoryBarrier") || !strcmp(#name, "glVertexAttribFormat") || \
		!strcmp(#name, "glVertexAttribIFormat") || !strcmp(#name, "glVertexAttribBinding") || \
		!strcmp(#name, "glBindVertexBuffer") || !strcmp(#name, "glGetQueryBufferObjectuiv") || \
		!strcmp(#name, "glBindTextures"))
#ifdef HALO_WEB
/* WebGL 2 is OpenGL ES 3.0: the 3.1 and 3.2 functions it lacks
(glCopyImageSubData, glDrawElementsBaseVertex, glMemoryBarrier) stay NULL,
and the renderer calls them only where its capabilities say the context
has them (xgpu_capabilities, d3d8_gl.c) */
#define GL_MISSING_FAILS 0
#else
#define GL_MISSING_FAILS 1
#endif
#define GL_LOAD_FUNCTION(name) \
	halo_##name = (__typeof__(halo_##name))SDL_GL_GetProcAddress(#name); \
	if (!halo_##name && !GL_OPTIONAL_FUNCTION(name)) \
	{ \
		if (GL_MISSING_FAILS) \
			platform_log("OpenGL function %s is unavailable", #name); \
		success = success && !GL_MISSING_FAILS; \
	}
	GL_FUNCTIONS(GL_LOAD_FUNCTION)
#undef GL_LOAD_FUNCTION
	return success;
}

#endif
