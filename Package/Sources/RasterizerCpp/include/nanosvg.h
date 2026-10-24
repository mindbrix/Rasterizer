/*
 * Copyright (c) 2013-14 Mikko Mononen memon@inside.org
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software
 * in a product, an acknowledgment in the product documentation would be
 * appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * The SVG parser is based on Anti-Grain Geometry 2.4 SVG example
 * Copyright (C) 2002-2004 Maxim Shemanarev (McSeem) (http://www.antigrain.com/)
 *
 * Arc calculation code based on canvg (https://code.google.com/p/canvg/)
 *
 * Bounding box calculation based on http://blog.hackers-cafe.net/2009/06/how-to-calculate-bezier-curves-bounding.html
 *
 */

#ifndef NANOSVG_H
#define NANOSVG_H

#ifdef __cplusplus
extern "C" {
#endif

// NanoSVG is a simple stupid single-header-file SVG parse. The output of the parser is a list of cubic bezier shapes.
//
// The library suits well for anything from rendering scalable icons in your editor application to prototyping a game.
//
// NanoSVG supports a wide range of SVG features, but something may be missing, feel free to create a pull request!
//
// The shapes in the SVG images are transformed by the viewBox and converted to specified units.
// That is, you should get the same looking data as your designed in your favorite app.
//
// NanoSVG can return the paths in few different units. For example if you want to render an image, you may choose
// to get the paths in pixels, or if you are feeding the data into a CNC-cutter, you may want to use millimeters.
//
// The units passed to NanoSVG should be one of: 'px', 'pt', 'pc' 'mm', 'cm', or 'in'.
// DPI (dots-per-inch) controls how the unit conversion is done.
//
// If you don't know or care about the units stuff, "px" and 96 should get you going.


/* Example Usage:
	// Load SVG
	NSVGimage* image;
	image = nsvgParseFromFile("test.svg", "px", 96);
	printf("size: %f x %f\n", image->width, image->height);
	// Use...
	for (NSVGshape *shape = image->shapes; shape != NULL; shape = shape->next) {
		for (NSVGpath *path = shape->paths; path != NULL; path = path->next) {
			for (int i = 0; i < path->npts-1; i += 3) {
				float* p = &path->pts[i*2];
				drawCubicBez(p[0],p[1], p[2],p[3], p[4],p[5], p[6],p[7]);
			}
		}
	}
	// Delete
	nsvgDelete(image);
*/

enum NSVGpaintType {
	NSVG_PAINT_NONE = 0,
	NSVG_PAINT_COLOR = 1,
	NSVG_PAINT_LINEAR_GRADIENT = 2,
	NSVG_PAINT_RADIAL_GRADIENT = 3
};

enum NSVGspreadType {
	NSVG_SPREAD_PAD = 0,
	NSVG_SPREAD_REFLECT = 1,
	NSVG_SPREAD_REPEAT = 2
};

enum NSVGlineJoin {
	NSVG_JOIN_MITER = 0,
	NSVG_JOIN_ROUND = 1,
	NSVG_JOIN_BEVEL = 2
};

enum NSVGlineCap {
	NSVG_CAP_BUTT = 0,
	NSVG_CAP_ROUND = 1,
	NSVG_CAP_SQUARE = 2
};

enum NSVGfillRule {
	NSVG_FILLRULE_NONZERO = 0,
	NSVG_FILLRULE_EVENODD = 1
};

enum NSVGflags {
	NSVG_FLAGS_VISIBLE = 0x01
};

typedef struct NSVGgradientStop {
	unsigned int color;
	float offset;
} NSVGgradientStop;

typedef struct NSVGgradient {
	float xform[6];
	char spread;
	float fx, fy;
	int nstops;
	NSVGgradientStop stops[1];
} NSVGgradient;

typedef struct NSVGpaint {
	char type;
	union {
		unsigned int color;
		NSVGgradient* gradient;
	};
} NSVGpaint;

typedef struct NSVGpath
{
	float* pts;					// Cubic bezier points: x0,y0, [cpx1,cpx1,cpx2,cpy2,x1,y1], ...
	int npts;					// Total number of bezier points.
	char closed;				// Flag indicating if shapes should be treated as closed.
	float bounds[4];			// Tight bounding box of the shape [minx,miny,maxx,maxy].
	struct NSVGpath* next;		// Pointer to next path, or NULL if last element.
} NSVGpath;

struct NSVGclipPath;

typedef struct NSVGshape
{
	char id[64];				// Optional 'id' attr of the shape or its group
	NSVGpaint fill;				// Fill paint
	NSVGpaint stroke;			// Stroke paint
	float opacity;				// Opacity of the shape.
	float strokeWidth;			// Stroke width (scaled).
	float strokeDashOffset;		// Stroke dash offset (scaled).
	float strokeDashArray[8];			// Stroke dash array (scaled).
	char strokeDashCount;				// Number of dash values in dash array.
	char strokeLineJoin;		// Stroke join type.
	char strokeLineCap;			// Stroke cap type.
	float miterLimit;			// Miter limit
	char fillRule;				// Fill rule, see NSVGfillRule.
	unsigned char flags;		// Logical or of NSVG_FLAGS_* flags
	float bounds[4];			// Tight bounding box of the shape [minx,miny,maxx,maxy].
	NSVGpath* paths;			// Linked list of paths in the image.
	struct NSVGshape* next;		// Pointer to next shape, or NULL if last element.
	struct NSVGclipPath** clipPaths;	// The clip paths whose intersection clips the shape, its own & its ancestors', or NULL.
	int clipPathCount;			// Number of clip paths.
} NSVGshape;

// A <clipPath>, as referred to by elements with the same transform, so they share it: the union of its shapes' fills, each by
// its fillRule, its clip-rule, in the image's coordinates. A clip path without shapes clips everything
typedef struct NSVGclipPath
{
	char id[64];				// The <clipPath>'s 'id' attr.
	NSVGshape* shapes;			// Linked list of its shapes.
	struct NSVGclipPath* next;	// Pointer to next clip path, or NULL if last element.
} NSVGclipPath;

typedef struct NSVGimage
{
	float width;				// Width of the image.
	float height;				// Height of the image.
	NSVGshape* shapes;			// Linked list of shapes in the image.
	NSVGclipPath* clipPaths;	// Linked list of the clip paths its shapes refer to.
} NSVGimage;

// Parses SVG file from a file, returns SVG image as paths.
NSVGimage* nsvgParseFromFile(const char* filename, const char* units, float dpi);

// Parses SVG file from a null terminated string, returns SVG image as paths.
// Important note: changes the string.
NSVGimage* nsvgParse(char* input, const char* units, float dpi);

// Duplicates a path.
NSVGpath* nsvgDuplicatePath(NSVGpath* p);

// Deletes an image.
void nsvgDelete(NSVGimage* image);

#ifdef __cplusplus
}
#endif

#endif // NANOSVG_H

#ifdef NANOSVG_IMPLEMENTATION

#include <string.h>
#include <stdlib.h>
#include <math.h>

#define NSVG_PI (3.14159265358979323846264338327f)
#define NSVG_KAPPA90 (0.5522847493f)	// Length proportional to radius of a cubic bezier handle for 90deg arcs.

#define NSVG_ALIGN_MIN 0
#define NSVG_ALIGN_MID 1
#define NSVG_ALIGN_MAX 2
#define NSVG_ALIGN_NONE 0
#define NSVG_ALIGN_MEET 1
#define NSVG_ALIGN_SLICE 2

#define NSVG_NOTUSED(v) do { (void)(1 ? (void)0 : ( (void)(v) ) ); } while(0)
#define NSVG_RGB(r, g, b) (((unsigned int)r) | ((unsigned int)g << 8) | ((unsigned int)b << 16))

#ifdef _MSC_VER
	#pragma warning (disable: 4996) // Switch off security warnings
	#pragma warning (disable: 4100) // Switch off unreferenced formal parameter warnings
	#ifdef __cplusplus
	#define NSVG_INLINE inline
	#else
	#define NSVG_INLINE
	#endif
#else
	#define NSVG_INLINE inline
#endif


// Matches '\0' too, like the strchr() it replaces, which parseStyle()'s trimming relies on
static int nsvg__isspace(char c)
{
	return c == ' ' || (c >= '\t' && c <= '\r') || c == '\0';
}

static int nsvg__isdigit(char c)
{
	return c >= '0' && c <= '9';
}

static int nsvg__isnum(char c)
{
	return nsvg__isdigit(c) || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E';
}

static NSVG_INLINE float nsvg__minf(float a, float b) { return a < b ? a : b; }
static NSVG_INLINE float nsvg__maxf(float a, float b) { return a > b ? a : b; }


// Simple XML parser

#define NSVG_XML_TAG 1
#define NSVG_XML_CONTENT 2
#define NSVG_XML_MAX_ATTRIBS 256

static void nsvg__parseContent(char* s,
							   void (*contentCb)(void* ud, const char* s),
							   void* ud)
{
	// Trim start white spaces
	while (*s && nsvg__isspace(*s)) s++;
	if (!*s) return;

	if (contentCb)
		(*contentCb)(ud, s);
}

static void nsvg__parseElement(char* s,
							   void (*startelCb)(void* ud, const char* el, const char** attr),
							   void (*endelCb)(void* ud, const char* el),
							   void* ud)
{
	const char* attr[NSVG_XML_MAX_ATTRIBS];
	int nattr = 0;
	char* name;
	int start = 0;
	int end = 0;
	char quote;

	// Skip white space after the '<'
	while (*s && nsvg__isspace(*s)) s++;

	// Check if the tag is end tag
	if (*s == '/') {
		s++;
		end = 1;
	} else {
		start = 1;
	}

	// Skip comments, data and preprocessor stuff.
	if (!*s || *s == '?' || *s == '!')
		return;

	// Get tag name
	name = s;
	while (*s && !nsvg__isspace(*s)) s++;
	if (*s) { *s++ = '\0'; }

	// Get attribs
	while (!end && *s && nattr < NSVG_XML_MAX_ATTRIBS-3) {
		char* name = NULL;
		char* value = NULL;

		// Skip white space before the attrib name
		while (*s && nsvg__isspace(*s)) s++;
		if (!*s) break;
		if (*s == '/') {
			end = 1;
			break;
		}
		name = s;
		// Find end of the attrib name.
		while (*s && !nsvg__isspace(*s) && *s != '=') s++;
		if (*s) { *s++ = '\0'; }
		// Skip until the beginning of the value.
		while (*s && *s != '\"' && *s != '\'') s++;
		if (!*s) break;
		quote = *s;
		s++;
		// Store value and find the end of it.
		value = s;
		s = strchr(s, quote);
		if (s) { *s++ = '\0'; } else { s = value + strlen(value); }

		// Store only well formed attributes
		if (name && value) {
			attr[nattr++] = name;
			attr[nattr++] = value;
		}
	}

	// List terminator
	attr[nattr++] = 0;
	attr[nattr++] = 0;

	// Call callbacks.
	if (start && startelCb)
		(*startelCb)(ud, name, attr);
	if (end && endelCb)
		(*endelCb)(ud, name);
}

int nsvg__parseXML(char* input,
				   void (*startelCb)(void* ud, const char* el, const char** attr),
				   void (*endelCb)(void* ud, const char* el),
				   void (*contentCb)(void* ud, const char* s),
				   void* ud)
{
	char* s = input;
	char* mark = s;
	int state = NSVG_XML_CONTENT;
	while ((s = strchr(s, state == NSVG_XML_CONTENT ? '<' : '>'))) {
		*s++ = '\0';
		if (state == NSVG_XML_CONTENT) {
			// Start of a tag
			nsvg__parseContent(mark, contentCb, ud);
			state = NSVG_XML_TAG;
		} else {
			// Start of a content or new tag.
			nsvg__parseElement(mark, startelCb, endelCb, ud);
			state = NSVG_XML_CONTENT;
		}
		mark = s;
	}

	return 1;
}


/* Simple SVG parser. */

#define NSVG_MAX_ATTR 128

enum NSVGgradientUnits {
	NSVG_USER_SPACE = 0,
	NSVG_OBJECT_SPACE = 1
};

#define NSVG_MAX_DASHES 8
#define NSVG_MAX_CLIPS 8	// Clip paths clipping a shape, from it & its ancestors

enum NSVGunits {
	NSVG_UNITS_USER,
	NSVG_UNITS_PX,
	NSVG_UNITS_PT,
	NSVG_UNITS_PC,
	NSVG_UNITS_MM,
	NSVG_UNITS_CM,
	NSVG_UNITS_IN,
	NSVG_UNITS_PERCENT,
	NSVG_UNITS_EM,
	NSVG_UNITS_EX
};

typedef struct NSVGcoordinate {
	float value;
	int units;
} NSVGcoordinate;

typedef struct NSVGlinearData {
	NSVGcoordinate x1, y1, x2, y2;
} NSVGlinearData;

typedef struct NSVGradialData {
	NSVGcoordinate cx, cy, r, fx, fy;
} NSVGradialData;

// The attributes a gradient sets, which those referring to it inherit if they don't
enum NSVGgradientAttribs {
	NSVG_GRAD_UNITS = 1 << 0, NSVG_GRAD_XFORM = 1 << 1, NSVG_GRAD_SPREAD = 1 << 2,
	NSVG_GRAD_X1 = 1 << 3, NSVG_GRAD_Y1 = 1 << 4, NSVG_GRAD_X2 = 1 << 5, NSVG_GRAD_Y2 = 1 << 6,
	NSVG_GRAD_CX = 1 << 7, NSVG_GRAD_CY = 1 << 8, NSVG_GRAD_R = 1 << 9, NSVG_GRAD_FX = 1 << 10, NSVG_GRAD_FY = 1 << 11
};

#define NSVG_MAX_GRADIENT_REFS 32	// How long a chain of gradient references is followed, which ends a cycle

typedef struct NSVGgradientData
{
	char id[64];
	char ref[64];
	int set;	// The NSVGgradientAttribs it sets
	char type;
	union {
		NSVGlinearData linear;
		NSVGradialData radial;
	};
	char spread;
	char units;
	float xform[6];
	int nstops;
	NSVGgradientStop* stops;
	struct NSVGgradientData* next;
} NSVGgradientData;

typedef struct NSVGattrib
{
	char id[64];
	float xform[6];
	unsigned int fillColor;
	unsigned int strokeColor;
	float opacity;
	float fillOpacity;
	float strokeOpacity;
	char fillGradient[64];
	char strokeGradient[64];
	float strokeWidth;
	float strokeDashOffset;
	float strokeDashArray[NSVG_MAX_DASHES];
	int strokeDashCount;
	char strokeLineJoin;
	char strokeLineCap;
	float miterLimit;
	char fillRule;
	float fontSize;
	unsigned int stopColor;
	float stopOpacity;
	float stopOffset;
	char hasFill;
	char hasStroke;
	char visible;
	char clipRule;
	char clipPathId[64];		// The element's own clip-path, until it's resolved, which its children don't inherit
	char deferClip;				// Whether its clip-path is resolved at its end, by its shapes' bounds, & the shape before them
	NSVGshape* clipShapes;
	NSVGclipPath* clips[NSVG_MAX_CLIPS];	// The clip paths of the element & its ancestors, which clip its shapes
	int nclips;
} NSVGattrib;

typedef struct NSVGparser
{
	NSVGattrib attr[NSVG_MAX_ATTR];
	int attrHead;
	float* pts;
	int npts;
	int cpts;
	NSVGpath* plist;
	NSVGimage* image;
	NSVGgradientData* gradients;
	NSVGshape* shapesTail;
	float viewMinx, viewMiny, viewWidth, viewHeight;
	int alignX, alignY, alignType;
	float dpi;
	char pathFlag;
	char defsFlag;
	char clipFlag;				// Whether a <clipPath>'s content is being parsed into its shapes
	struct NSVGdocument* doc;	// The elements, by id, for <use> & clip-path
	struct NSVGclipCache* clipCache;
} NSVGparser;

static void nsvg__xformIdentity(float* t)
{
	t[0] = 1.0f; t[1] = 0.0f;
	t[2] = 0.0f; t[3] = 1.0f;
	t[4] = 0.0f; t[5] = 0.0f;
}

static void nsvg__xformSetTranslation(float* t, float tx, float ty)
{
	t[0] = 1.0f; t[1] = 0.0f;
	t[2] = 0.0f; t[3] = 1.0f;
	t[4] = tx; t[5] = ty;
}

static void nsvg__xformSetScale(float* t, float sx, float sy)
{
	t[0] = sx; t[1] = 0.0f;
	t[2] = 0.0f; t[3] = sy;
	t[4] = 0.0f; t[5] = 0.0f;
}

static void nsvg__xformSetSkewX(float* t, float a)
{
	t[0] = 1.0f; t[1] = 0.0f;
	t[2] = tanf(a); t[3] = 1.0f;
	t[4] = 0.0f; t[5] = 0.0f;
}

static void nsvg__xformSetSkewY(float* t, float a)
{
	t[0] = 1.0f; t[1] = tanf(a);
	t[2] = 0.0f; t[3] = 1.0f;
	t[4] = 0.0f; t[5] = 0.0f;
}

static void nsvg__xformSetRotation(float* t, float a)
{
	float cs = cosf(a), sn = sinf(a);
	t[0] = cs; t[1] = sn;
	t[2] = -sn; t[3] = cs;
	t[4] = 0.0f; t[5] = 0.0f;
}

static void nsvg__xformMultiply(float* t, float* s)
{
	float t0 = t[0] * s[0] + t[1] * s[2];
	float t2 = t[2] * s[0] + t[3] * s[2];
	float t4 = t[4] * s[0] + t[5] * s[2] + s[4];
	t[1] = t[0] * s[1] + t[1] * s[3];
	t[3] = t[2] * s[1] + t[3] * s[3];
	t[5] = t[4] * s[1] + t[5] * s[3] + s[5];
	t[0] = t0;
	t[2] = t2;
	t[4] = t4;
}

static void nsvg__xformInverse(float* inv, float* t)
{
	double invdet, det = (double)t[0] * t[3] - (double)t[2] * t[1];
	if (det > -1e-6 && det < 1e-6) {
		nsvg__xformIdentity(t);
		return;
	}
	invdet = 1.0 / det;
	inv[0] = (float)(t[3] * invdet);
	inv[2] = (float)(-t[2] * invdet);
	inv[4] = (float)(((double)t[2] * t[5] - (double)t[3] * t[4]) * invdet);
	inv[1] = (float)(-t[1] * invdet);
	inv[3] = (float)(t[0] * invdet);
	inv[5] = (float)(((double)t[1] * t[4] - (double)t[0] * t[5]) * invdet);
}

static void nsvg__xformPremultiply(float* t, float* s)
{
	float s2[6];
	memcpy(s2, s, sizeof(float)*6);
	nsvg__xformMultiply(s2, t);
	memcpy(t, s2, sizeof(float)*6);
}

static void nsvg__xformPoint(float* dx, float* dy, float x, float y, float* t)
{
	*dx = x*t[0] + y*t[2] + t[4];
	*dy = x*t[1] + y*t[3] + t[5];
}

static void nsvg__xformVec(float* dx, float* dy, float x, float y, float* t)
{
	*dx = x*t[0] + y*t[2];
	*dy = x*t[1] + y*t[3];
}

#define NSVG_EPSILON (1e-12)

static int nsvg__ptInBounds(float* pt, float* bounds)
{
	return pt[0] >= bounds[0] && pt[0] <= bounds[2] && pt[1] >= bounds[1] && pt[1] <= bounds[3];
}


static double nsvg__evalBezier(double t, double p0, double p1, double p2, double p3)
{
	double it = 1.0-t;
	return it*it*it*p0 + 3.0*it*it*t*p1 + 3.0*it*t*t*p2 + t*t*t*p3;
}

static void nsvg__curveBounds(float* bounds, float* curve)
{
	int i, j, count;
	double roots[2], a, b, c, b2ac, t, v;
	float* v0 = &curve[0];
	float* v1 = &curve[2];
	float* v2 = &curve[4];
	float* v3 = &curve[6];

	// Start the bounding box by end points
	bounds[0] = nsvg__minf(v0[0], v3[0]);
	bounds[1] = nsvg__minf(v0[1], v3[1]);
	bounds[2] = nsvg__maxf(v0[0], v3[0]);
	bounds[3] = nsvg__maxf(v0[1], v3[1]);

	// Bezier curve fits inside the convex hull of it's control points.
	// If control points are inside the bounds, we're done.
	if (nsvg__ptInBounds(v1, bounds) && nsvg__ptInBounds(v2, bounds))
		return;

	// Add bezier curve inflection points in X and Y.
	for (i = 0; i < 2; i++) {
		a = -3.0 * v0[i] + 9.0 * v1[i] - 9.0 * v2[i] + 3.0 * v3[i];
		b = 6.0 * v0[i] - 12.0 * v1[i] + 6.0 * v2[i];
		c = 3.0 * v1[i] - 3.0 * v0[i];
		count = 0;
		if (fabs(a) < NSVG_EPSILON) {
			if (fabs(b) > NSVG_EPSILON) {
				t = -c / b;
				if (t > NSVG_EPSILON && t < 1.0-NSVG_EPSILON)
					roots[count++] = t;
			}
		} else {
			b2ac = b*b - 4.0*c*a;
			if (b2ac > NSVG_EPSILON) {
				t = (-b + sqrt(b2ac)) / (2.0 * a);
				if (t > NSVG_EPSILON && t < 1.0-NSVG_EPSILON)
					roots[count++] = t;
				t = (-b - sqrt(b2ac)) / (2.0 * a);
				if (t > NSVG_EPSILON && t < 1.0-NSVG_EPSILON)
					roots[count++] = t;
			}
		}
		for (j = 0; j < count; j++) {
			v = nsvg__evalBezier(roots[j], v0[i], v1[i], v2[i], v3[i]);
			bounds[0+i] = nsvg__minf(bounds[0+i], (float)v);
			bounds[2+i] = nsvg__maxf(bounds[2+i], (float)v);
		}
	}
}

static NSVGparser* nsvg__createParser()
{
	NSVGparser* p;
	p = (NSVGparser*)malloc(sizeof(NSVGparser));
	if (p == NULL) goto error;
	memset(p, 0, sizeof(NSVGparser));

	p->image = (NSVGimage*)malloc(sizeof(NSVGimage));
	if (p->image == NULL) goto error;
	memset(p->image, 0, sizeof(NSVGimage));

	// Init style
	nsvg__xformIdentity(p->attr[0].xform);
	memset(p->attr[0].id, 0, sizeof p->attr[0].id);
	p->attr[0].fillColor = NSVG_RGB(0,0,0);
	p->attr[0].strokeColor = NSVG_RGB(0,0,0);
	p->attr[0].opacity = 1;
	p->attr[0].fillOpacity = 1;
	p->attr[0].strokeOpacity = 1;
	p->attr[0].stopOpacity = 1;
	p->attr[0].strokeWidth = 1;
	p->attr[0].strokeLineJoin = NSVG_JOIN_MITER;
	p->attr[0].strokeLineCap = NSVG_CAP_BUTT;
	p->attr[0].miterLimit = 4;
	p->attr[0].fillRule = NSVG_FILLRULE_NONZERO;
	p->attr[0].clipRule = NSVG_FILLRULE_NONZERO;
	p->attr[0].hasFill = 1;
	p->attr[0].visible = 1;

	return p;

error:
	if (p) {
		if (p->image) free(p->image);
		free(p);
	}
	return NULL;
}

static void nsvg__deletePaths(NSVGpath* path)
{
	while (path) {
		NSVGpath *next = path->next;
		if (path->pts != NULL)
			free(path->pts);
		free(path);
		path = next;
	}
}

static void nsvg__deletePaint(NSVGpaint* paint)
{
	if (paint->type == NSVG_PAINT_LINEAR_GRADIENT || paint->type == NSVG_PAINT_RADIAL_GRADIENT)
		free(paint->gradient);
}

static void nsvg__deleteGradientData(NSVGgradientData* grad)
{
	NSVGgradientData* next;
	while (grad != NULL) {
		next = grad->next;
		free(grad->stops);
		free(grad);
		grad = next;
	}
}

struct NSVGclipCache;
static void nsvg__deleteClipCache(struct NSVGclipCache* c);

static void nsvg__deleteParser(NSVGparser* p)
{
	if (p != NULL) {
		nsvg__deleteClipCache(p->clipCache);
		nsvg__deletePaths(p->plist);
		nsvg__deleteGradientData(p->gradients);
		nsvgDelete(p->image);
		free(p->pts);
		free(p);
	}
}

static void nsvg__resetPath(NSVGparser* p)
{
	p->npts = 0;
}

static void nsvg__addPoint(NSVGparser* p, float x, float y)
{
	if (p->npts+1 > p->cpts) {
		p->cpts = p->cpts ? p->cpts*2 : 8;
		p->pts = (float*)realloc(p->pts, p->cpts*2*sizeof(float));
		if (!p->pts) return;
	}
	p->pts[p->npts*2+0] = x;
	p->pts[p->npts*2+1] = y;
	p->npts++;
}

static void nsvg__moveTo(NSVGparser* p, float x, float y)
{
	if (p->npts > 0) {
		p->pts[(p->npts-1)*2+0] = x;
		p->pts[(p->npts-1)*2+1] = y;
	} else {
		nsvg__addPoint(p, x, y);
	}
}

static void nsvg__lineTo(NSVGparser* p, float x, float y)
{
	float px,py, dx,dy;
	if (p->npts > 0) {
		px = p->pts[(p->npts-1)*2+0];
		py = p->pts[(p->npts-1)*2+1];
		dx = x - px;
		dy = y - py;
		nsvg__addPoint(p, px + dx/3.0f, py + dy/3.0f);
		nsvg__addPoint(p, x - dx/3.0f, y - dy/3.0f);
		nsvg__addPoint(p, x, y);
	}
}

static void nsvg__cubicBezTo(NSVGparser* p, float cpx1, float cpy1, float cpx2, float cpy2, float x, float y)
{
	if (p->npts == 0)	// No current point, like nsvg__lineTo(), else npts isn't 3n+1 & nsvg__addPath() overruns pts
		return;
	nsvg__addPoint(p, cpx1, cpy1);
	nsvg__addPoint(p, cpx2, cpy2);
	nsvg__addPoint(p, x, y);
}

static NSVGattrib* nsvg__getAttr(NSVGparser* p)
{
	return &p->attr[p->attrHead];
}

static void nsvg__pushAttr(NSVGparser* p)
{
	if (p->attrHead < NSVG_MAX_ATTR-1) {
		p->attrHead++;
		memcpy(&p->attr[p->attrHead], &p->attr[p->attrHead-1], sizeof(NSVGattrib));
		p->attr[p->attrHead].clipPathId[0] = '\0';	// A clip-path isn't inherited, but its clip is, in clips
		p->attr[p->attrHead].deferClip = 0;
	}
}

static void nsvg__popAttr(NSVGparser* p)
{
	if (p->attrHead > 0)
		p->attrHead--;
}

static float nsvg__actualOrigX(NSVGparser* p)
{
	return p->viewMinx;
}

static float nsvg__actualOrigY(NSVGparser* p)
{
	return p->viewMiny;
}

static float nsvg__actualWidth(NSVGparser* p)
{
	return p->viewWidth;
}

static float nsvg__actualHeight(NSVGparser* p)
{
	return p->viewHeight;
}

static float nsvg__actualLength(NSVGparser* p)
{
	float w = nsvg__actualWidth(p), h = nsvg__actualHeight(p);
	return sqrtf(w*w + h*h) / sqrtf(2.0f);
}

static float nsvg__convertToPixels(NSVGparser* p, NSVGcoordinate c, float orig, float length)
{
	NSVGattrib* attr = nsvg__getAttr(p);
	switch (c.units) {
		case NSVG_UNITS_USER:		return c.value;
		case NSVG_UNITS_PX:			return c.value;
		case NSVG_UNITS_PT:			return c.value / 72.0f * p->dpi;
		case NSVG_UNITS_PC:			return c.value / 6.0f * p->dpi;
		case NSVG_UNITS_MM:			return c.value / 25.4f * p->dpi;
		case NSVG_UNITS_CM:			return c.value / 2.54f * p->dpi;
		case NSVG_UNITS_IN:			return c.value * p->dpi;
		case NSVG_UNITS_EM:			return c.value * attr->fontSize;
		case NSVG_UNITS_EX:			return c.value * attr->fontSize * 0.52f; // x-height of Helvetica.
		case NSVG_UNITS_PERCENT:	return orig + c.value / 100.0f * length;
		default:					return c.value;
	}
	return c.value;
}

static NSVGgradientData* nsvg__findGradientData(NSVGparser* p, const char* id)
{
	NSVGgradientData* grad = p->gradients;
	if (id[0] == '\0')
		return NULL;	// No reference, which mustn't find a gradient without an id
	while (grad) {
		if (strcmp(grad->id, id) == 0)
			return grad;
		grad = grad->next;
	}
	return NULL;
}

// A gradient coordinate: in object space, a fraction of the shape's bounds, given as a number or a percentage
static float nsvg__gradientCoord(NSVGparser* p, NSVGcoordinate c, float orig, float length, int objectSpace)
{
	if (objectSpace)
		return c.units == NSVG_UNITS_PERCENT ? c.value / 100.0f : c.value;
	return nsvg__convertToPixels(p, c, orig, length);
}

// In object space, the gradient is built in the unit square of the shape's bounds, where its gradientTransform applies, & is
// then mapped to the bounds, so a radial gradient of a non-square shape is elliptical
static NSVGgradient* nsvg__createGradient(NSVGparser* p, const char* id, const float* localBounds, char* paintType)
{
	NSVGattrib* attr = nsvg__getAttr(p);
	NSVGgradientData* found = NULL;
	NSVGgradientData* ref = NULL;
	NSVGgradientStop* stops = NULL;
	NSVGgradient* grad;
	NSVGgradientData resolved, *data = &resolved;
	float ox, oy, sw, sh, sl;
	int nstops = 0, objectSpace, depth;

	found = nsvg__findGradientData(p, id);
	if (found == NULL) return NULL;

	// The first stops along the references, & each attribute it doesn't set from the first that does, but a gradient of the
	// other type only has its units, transform & spread inherited
	resolved = *found;
	for (ref = found, depth = 0; ref != NULL && depth < NSVG_MAX_GRADIENT_REFS; ref = nsvg__findGradientData(p, ref->ref), depth++) {
		int inherit = ref->set & ~resolved.set & (ref->type == resolved.type ? ~0 : NSVG_GRAD_UNITS | NSVG_GRAD_XFORM | NSVG_GRAD_SPREAD);
		if (stops == NULL && ref->stops != NULL) {
			stops = ref->stops;
			nstops = ref->nstops;
		}
		if (inherit & NSVG_GRAD_UNITS) resolved.units = ref->units;
		if (inherit & NSVG_GRAD_XFORM) memcpy(resolved.xform, ref->xform, sizeof(resolved.xform));
		if (inherit & NSVG_GRAD_SPREAD) resolved.spread = ref->spread;
		if (inherit & NSVG_GRAD_X1) resolved.linear.x1 = ref->linear.x1;
		if (inherit & NSVG_GRAD_Y1) resolved.linear.y1 = ref->linear.y1;
		if (inherit & NSVG_GRAD_X2) resolved.linear.x2 = ref->linear.x2;
		if (inherit & NSVG_GRAD_Y2) resolved.linear.y2 = ref->linear.y2;
		if (inherit & NSVG_GRAD_CX) resolved.radial.cx = ref->radial.cx;
		if (inherit & NSVG_GRAD_CY) resolved.radial.cy = ref->radial.cy;
		if (inherit & NSVG_GRAD_R) resolved.radial.r = ref->radial.r;
		if (inherit & NSVG_GRAD_FX) resolved.radial.fx = ref->radial.fx;
		if (inherit & NSVG_GRAD_FY) resolved.radial.fy = ref->radial.fy;
		resolved.set |= inherit;
	}
	if (stops == NULL) return NULL;
	if (resolved.type == NSVG_PAINT_RADIAL_GRADIENT) {	// The focus is the centre by default
		if (!(resolved.set & NSVG_GRAD_FX)) resolved.radial.fx = resolved.radial.cx;
		if (!(resolved.set & NSVG_GRAD_FY)) resolved.radial.fy = resolved.radial.cy;
	}
	if (data->units == NSVG_OBJECT_SPACE && (localBounds[2] <= localBounds[0] || localBounds[3] <= localBounds[1]))
		return NULL;	// Bounds without width or height have no unit square, so the gradient is ignored

	grad = (NSVGgradient*)malloc(sizeof(NSVGgradient) + sizeof(NSVGgradientStop)*(nstops-1));
	if (grad == NULL) return NULL;

	// The shape width and height.
	objectSpace = data->units == NSVG_OBJECT_SPACE;
	if (objectSpace) {
		ox = localBounds[0];
		oy = localBounds[1];
		sw = localBounds[2] - localBounds[0];
		sh = localBounds[3] - localBounds[1];
	} else {
		ox = nsvg__actualOrigX(p);
		oy = nsvg__actualOrigY(p);
		sw = nsvg__actualWidth(p);
		sh = nsvg__actualHeight(p);
	}
	sl = sqrtf(sw*sw + sh*sh) / sqrtf(2.0f);

	if (data->type == NSVG_PAINT_LINEAR_GRADIENT) {
		float x1, y1, x2, y2, dx, dy;
		x1 = nsvg__gradientCoord(p, data->linear.x1, ox, sw, objectSpace);
		y1 = nsvg__gradientCoord(p, data->linear.y1, oy, sh, objectSpace);
		x2 = nsvg__gradientCoord(p, data->linear.x2, ox, sw, objectSpace);
		y2 = nsvg__gradientCoord(p, data->linear.y2, oy, sh, objectSpace);
		// Calculate transform aligned to the line
		dx = x2 - x1;
		dy = y2 - y1;
		grad->xform[0] = dy; grad->xform[1] = -dx;
		grad->xform[2] = dx; grad->xform[3] = dy;
		grad->xform[4] = x1; grad->xform[5] = y1;
	} else {
		float cx, cy, fx, fy, r;
		cx = nsvg__gradientCoord(p, data->radial.cx, ox, sw, objectSpace);
		cy = nsvg__gradientCoord(p, data->radial.cy, oy, sh, objectSpace);
		fx = nsvg__gradientCoord(p, data->radial.fx, ox, sw, objectSpace);
		fy = nsvg__gradientCoord(p, data->radial.fy, oy, sh, objectSpace);
		r = nsvg__gradientCoord(p, data->radial.r, 0, sl, objectSpace);
		// Calculate transform aligned to the circle
		grad->xform[0] = r; grad->xform[1] = 0;
		grad->xform[2] = 0; grad->xform[3] = r;
		grad->xform[4] = cx; grad->xform[5] = cy;
		grad->fx = fx / r;
		grad->fy = fy / r;
	}

	nsvg__xformMultiply(grad->xform, data->xform);
	if (objectSpace) {
		float bounds[6] = { sw, 0.0f, 0.0f, sh, ox, oy };
		nsvg__xformMultiply(grad->xform, bounds);
	}
	nsvg__xformMultiply(grad->xform, attr->xform);

	grad->spread = data->spread;
	memcpy(grad->stops, stops, nstops*sizeof(NSVGgradientStop));
	grad->nstops = nstops;

	*paintType = data->type;

	return grad;
}

static float nsvg__getAverageScale(float* t)
{
	float sx = sqrtf(t[0]*t[0] + t[2]*t[2]);
	float sy = sqrtf(t[1]*t[1] + t[3]*t[3]);
	return (sx + sy) * 0.5f;
}

static void nsvg__getLocalBounds(float* bounds, NSVGshape *shape, float* xform)
{
	NSVGpath* path;
	float curve[4*2], curveBounds[4];
	int i, first = 1;
	for (path = shape->paths; path != NULL; path = path->next) {
		nsvg__xformPoint(&curve[0], &curve[1], path->pts[0], path->pts[1], xform);
		for (i = 0; i < path->npts-1; i += 3) {
			nsvg__xformPoint(&curve[2], &curve[3], path->pts[(i+1)*2], path->pts[(i+1)*2+1], xform);
			nsvg__xformPoint(&curve[4], &curve[5], path->pts[(i+2)*2], path->pts[(i+2)*2+1], xform);
			nsvg__xformPoint(&curve[6], &curve[7], path->pts[(i+3)*2], path->pts[(i+3)*2+1], xform);
			nsvg__curveBounds(curveBounds, curve);
			if (first) {
				bounds[0] = curveBounds[0];
				bounds[1] = curveBounds[1];
				bounds[2] = curveBounds[2];
				bounds[3] = curveBounds[3];
				first = 0;
			} else {
				bounds[0] = nsvg__minf(bounds[0], curveBounds[0]);
				bounds[1] = nsvg__minf(bounds[1], curveBounds[1]);
				bounds[2] = nsvg__maxf(bounds[2], curveBounds[2]);
				bounds[3] = nsvg__maxf(bounds[3], curveBounds[3]);
			}
			curve[0] = curve[6];
			curve[1] = curve[7];
		}
	}
}

static NSVGclipPath* nsvg__clipInstance(NSVGparser* p, const char* id, const float* xform, const float* bounds);
static void nsvg__addClip(NSVGattrib* attr, NSVGclipPath* clip);

static void nsvg__addShape(NSVGparser* p)
{
	NSVGattrib* attr = nsvg__getAttr(p);
	float scale = 1.0f;
	NSVGshape* shape;
	NSVGpath* path;
	int i;

	if (p->plist == NULL)
		return;

	shape = (NSVGshape*)malloc(sizeof(NSVGshape));
	if (shape == NULL) goto error;
	memset(shape, 0, sizeof(NSVGshape));

	memcpy(shape->id, attr->id, sizeof shape->id);
	scale = nsvg__getAverageScale(attr->xform);
	shape->strokeWidth = attr->strokeWidth * scale;
	shape->strokeDashOffset = attr->strokeDashOffset * scale;
	shape->strokeDashCount = (char)attr->strokeDashCount;
	for (i = 0; i < attr->strokeDashCount; i++)
		shape->strokeDashArray[i] = attr->strokeDashArray[i] * scale;
	shape->strokeLineJoin = attr->strokeLineJoin;
	shape->strokeLineCap = attr->strokeLineCap;
	shape->miterLimit = attr->miterLimit;
	shape->fillRule = attr->fillRule;
	shape->opacity = attr->opacity;

	shape->paths = p->plist;
	p->plist = NULL;

	// Calculate shape bounds
	shape->bounds[0] = shape->paths->bounds[0];
	shape->bounds[1] = shape->paths->bounds[1];
	shape->bounds[2] = shape->paths->bounds[2];
	shape->bounds[3] = shape->paths->bounds[3];
	for (path = shape->paths->next; path != NULL; path = path->next) {
		shape->bounds[0] = nsvg__minf(shape->bounds[0], path->bounds[0]);
		shape->bounds[1] = nsvg__minf(shape->bounds[1], path->bounds[1]);
		shape->bounds[2] = nsvg__maxf(shape->bounds[2], path->bounds[2]);
		shape->bounds[3] = nsvg__maxf(shape->bounds[3], path->bounds[3]);
	}

	// Set fill
	if (attr->hasFill == 0) {
		shape->fill.type = NSVG_PAINT_NONE;
	} else if (attr->hasFill == 1) {
		shape->fill.type = NSVG_PAINT_COLOR;
		shape->fill.color = attr->fillColor;
		shape->fill.color |= (unsigned int)(attr->fillOpacity*255) << 24;
	} else if (attr->hasFill == 2) {
		float inv[6], localBounds[4];
		nsvg__xformInverse(inv, attr->xform);
		nsvg__getLocalBounds(localBounds, shape, inv);
		shape->fill.gradient = nsvg__createGradient(p, attr->fillGradient, localBounds, &shape->fill.type);
		if (shape->fill.gradient == NULL) {
			shape->fill.type = NSVG_PAINT_NONE;
		}
	}

	// Set stroke
	if (attr->hasStroke == 0) {
		shape->stroke.type = NSVG_PAINT_NONE;
	} else if (attr->hasStroke == 1) {
		shape->stroke.type = NSVG_PAINT_COLOR;
		shape->stroke.color = attr->strokeColor;
		shape->stroke.color |= (unsigned int)(attr->strokeOpacity*255) << 24;
	} else if (attr->hasStroke == 2) {
		float inv[6], localBounds[4];
		nsvg__xformInverse(inv, attr->xform);
		nsvg__getLocalBounds(localBounds, shape, inv);
		shape->stroke.gradient = nsvg__createGradient(p, attr->strokeGradient, localBounds, &shape->stroke.type);
		if (shape->stroke.gradient == NULL)
			shape->stroke.type = NSVG_PAINT_NONE;
	}

	// Set flags
	shape->flags = (attr->visible ? NSVG_FLAGS_VISIBLE : 0x00);

	if (p->clipFlag) {
		// A clip path's shapes are their fills' geometry, by their clip-rule
		nsvg__deletePaint(&shape->fill);
		nsvg__deletePaint(&shape->stroke);
		shape->fill.type = NSVG_PAINT_COLOR;
		shape->fill.color = NSVG_RGB(0, 0, 0) | 0xFF000000u;
		shape->stroke.type = NSVG_PAINT_NONE;
		shape->fillRule = attr->clipRule;
	} else {
		// Its own clip-path, in its user space, & its ancestors'
		if (attr->clipPathId[0]) {
			float inv[6], localBounds[4];
			nsvg__xformInverse(inv, attr->xform);
			nsvg__getLocalBounds(localBounds, shape, inv);
			nsvg__addClip(attr, nsvg__clipInstance(p, attr->clipPathId, attr->xform, localBounds));
			attr->clipPathId[0] = '\0';
		}
		if (attr->nclips) {
			shape->clipPaths = (NSVGclipPath**)malloc(attr->nclips * sizeof(NSVGclipPath*));
			if (shape->clipPaths) {
				memcpy(shape->clipPaths, attr->clips, attr->nclips * sizeof(NSVGclipPath*));
				shape->clipPathCount = attr->nclips;
			}
		}
	}

	// Add to tail
	if (p->image->shapes == NULL)
		p->image->shapes = shape;
	else
		p->shapesTail->next = shape;
	p->shapesTail = shape;

	return;

error:
	if (shape) free(shape);
}

static void nsvg__addPath(NSVGparser* p, char closed)
{
	NSVGattrib* attr = nsvg__getAttr(p);
	NSVGpath* path = NULL;
	float bounds[4];
	float* curve;
	int i;

	// "M x y z" is a zero-length closed subpath, which strokes as a cap-shaped dot, so keep it as a zero-length line.
	if (closed && p->npts == 1)
		nsvg__lineTo(p, p->pts[0], p->pts[1]);
	if (p->npts < 4)
		return;

	if (closed)
		nsvg__lineTo(p, p->pts[0], p->pts[1]);

	path = (NSVGpath*)malloc(sizeof(NSVGpath));
	if (path == NULL) goto error;
	memset(path, 0, sizeof(NSVGpath));

	path->pts = (float*)malloc(p->npts*2*sizeof(float));
	if (path->pts == NULL) goto error;
	path->closed = closed;
	path->npts = p->npts;

	// Transform path.
	for (i = 0; i < p->npts; ++i)
		nsvg__xformPoint(&path->pts[i*2], &path->pts[i*2+1], p->pts[i*2], p->pts[i*2+1], attr->xform);

	// Find bounds
	for (i = 0; i < path->npts-1; i += 3) {
		curve = &path->pts[i*2];
		nsvg__curveBounds(bounds, curve);
		if (i == 0) {
			path->bounds[0] = bounds[0];
			path->bounds[1] = bounds[1];
			path->bounds[2] = bounds[2];
			path->bounds[3] = bounds[3];
		} else {
			path->bounds[0] = nsvg__minf(path->bounds[0], bounds[0]);
			path->bounds[1] = nsvg__minf(path->bounds[1], bounds[1]);
			path->bounds[2] = nsvg__maxf(path->bounds[2], bounds[2]);
			path->bounds[3] = nsvg__maxf(path->bounds[3], bounds[3]);
		}
	}

	path->next = p->plist;
	p->plist = path;

	return;

error:
	if (path != NULL) {
		if (path->pts != NULL) free(path->pts);
		free(path);
	}
}

// We roll our own string to float because the std library one uses locale and messes things up.
// Scans [+-]digits[.digits][e[+-]digits], returning the end & the value in *value, which is 0 without digits.
// Digits are summed exactly as integers, at most 18 of them in each part, & then scaled by exact powers of 10.
// Unless strict, an exponent without digits is skipped too, so "1em" ends before "em" only when strict.
static const char* nsvg__parseNumberValue(const char* s, double* value, int strict)
{
	static const double pow10[] = { 1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18 };
	double res = 0.0, sign = 1.0;
	unsigned long long intPart = 0, fracPart = 0;
	int intDigits = 0, extraDigits = 0, fracDigits = 0, digits = 0;

	// Parse optional sign
	if (*s == '+') {
		s++;
	} else if (*s == '-') {
		sign = -1;
		s++;
	}
	// Parse integer part
	for (; nsvg__isdigit(*s); s++, digits++) {
		if (intDigits < 18)
			intPart = intPart * 10 + (*s - '0'), intDigits += intPart != 0;
		else
			extraDigits++;
	}
	res = (double)intPart;
	if (extraDigits)
		res *= extraDigits <= 18 ? pow10[extraDigits] : pow(10.0, (double)extraDigits);
	// Parse fractional part.
	if (*s == '.') {
		for (s++; nsvg__isdigit(*s); s++, digits++)
			if (fracDigits < 18)
				fracPart = fracPart * 10 + (*s - '0'), fracDigits++;
		res += (double)fracPart / pow10[fracDigits];
	}
	// Parse optional exponent
	const char* e = s;
	if (*e == 'e' || *e == 'E') {
		int expPart = 0, expSign = 1;
		e++; // skip 'E'
		if (*e == '+') {
			e++;
		} else if (*e == '-') {
			expSign = -1;
			e++;
		}
		if (nsvg__isdigit(*e)) {
			for (; nsvg__isdigit(*e); e++)
				if (expPart < 100000)
					expPart = expPart * 10 + (*e - '0');
			res *= pow(10.0, (double)(expSign * expPart));
			s = e;
		} else if (!strict)
			s = e;
	}
	// A valid number should have integer or fractional part.
	*value = digits ? res * sign : 0.0;
	return s;
}

static double nsvg__atof(const char* s)
{
	double value;
	nsvg__parseNumberValue(s, &value, 1);
	return value;
}


// Returns a command in it, or a number as its first character in it & its value in *value
static const char* nsvg__getNextPathItem(const char* s, char* it, float* value)
{
	double v = 0.0;
	it[0] = '\0';
	*value = 0.0f;
	// Skip white spaces and commas
	while (*s && (nsvg__isspace(*s) || *s == ',')) s++;
	if (!*s) return s;
	if (*s == '-' || *s == '+' || *s == '.' || nsvg__isdigit(*s)) {
		it[0] = *s;
		it[1] = '\0';
		s = nsvg__parseNumberValue(s, &v, 0);
		*value = (float)v;
	} else {
		// Parse command
		it[0] = *s++;
		it[1] = '\0';
		return s;
	}

	return s;
}

// Like sscanf %x: hex digits up to the first non-hex character
static unsigned int nsvg__parseHex(const char* str)
{
	unsigned int c = 0;
	for (;; str++) {
		if (*str >= '0' && *str <= '9')
			c = c * 16 + (*str - '0');
		else if ((*str | 0x20) >= 'a' && (*str | 0x20) <= 'f')
			c = c * 16 + ((*str | 0x20) - 'a' + 10);
		else
			return c;
	}
}

static unsigned int nsvg__parseColorHex(const char* str)
{
	unsigned int c = 0, r = 0, g = 0, b = 0;
	int n = 0;
	str++; // skip #
	// Calculate number of characters.
	while(str[n] && !nsvg__isspace(str[n]))
		n++;
	if (n == 6) {
		c = nsvg__parseHex(str);
	} else if (n == 3) {
		c = nsvg__parseHex(str);
		c = (c&0xf) | ((c&0xf0) << 4) | ((c&0xf00) << 8);
		c |= c<<4;
	}
	r = (c >> 16) & 0xff;
	g = (c >> 8) & 0xff;
	b = c & 0xff;
	return NSVG_RGB(r,g,b);
}

static unsigned int nsvg__parseColorRGB(const char* str)
{
	int r = -1, g = -1, b = -1;
	char s1[32]="", s2[32]="";
	sscanf(str + 4, "%d%[%%, \t]%d%[%%, \t]%d", &r, s1, &g, s2, &b);
	if (strchr(s1, '%')) {
		return NSVG_RGB((r*255)/100,(g*255)/100,(b*255)/100);
	} else {
		return NSVG_RGB(r,g,b);
	}
}

typedef struct NSVGNamedColor {
	const char* name;
	unsigned int color;
} NSVGNamedColor;

NSVGNamedColor nsvg__colors[] = {

	{ "red", NSVG_RGB(255, 0, 0) },
	{ "green", NSVG_RGB( 0, 128, 0) },
	{ "blue", NSVG_RGB( 0, 0, 255) },
	{ "yellow", NSVG_RGB(255, 255, 0) },
	{ "cyan", NSVG_RGB( 0, 255, 255) },
	{ "magenta", NSVG_RGB(255, 0, 255) },
	{ "black", NSVG_RGB( 0, 0, 0) },
	{ "grey", NSVG_RGB(128, 128, 128) },
	{ "gray", NSVG_RGB(128, 128, 128) },
	{ "white", NSVG_RGB(255, 255, 255) },

#ifdef NANOSVG_ALL_COLOR_KEYWORDS
	{ "aliceblue", NSVG_RGB(240, 248, 255) },
	{ "antiquewhite", NSVG_RGB(250, 235, 215) },
	{ "aqua", NSVG_RGB( 0, 255, 255) },
	{ "aquamarine", NSVG_RGB(127, 255, 212) },
	{ "azure", NSVG_RGB(240, 255, 255) },
	{ "beige", NSVG_RGB(245, 245, 220) },
	{ "bisque", NSVG_RGB(255, 228, 196) },
	{ "blanchedalmond", NSVG_RGB(255, 235, 205) },
	{ "blueviolet", NSVG_RGB(138, 43, 226) },
	{ "brown", NSVG_RGB(165, 42, 42) },
	{ "burlywood", NSVG_RGB(222, 184, 135) },
	{ "cadetblue", NSVG_RGB( 95, 158, 160) },
	{ "chartreuse", NSVG_RGB(127, 255, 0) },
	{ "chocolate", NSVG_RGB(210, 105, 30) },
	{ "coral", NSVG_RGB(255, 127, 80) },
	{ "cornflowerblue", NSVG_RGB(100, 149, 237) },
	{ "cornsilk", NSVG_RGB(255, 248, 220) },
	{ "crimson", NSVG_RGB(220, 20, 60) },
	{ "darkblue", NSVG_RGB( 0, 0, 139) },
	{ "darkcyan", NSVG_RGB( 0, 139, 139) },
	{ "darkgoldenrod", NSVG_RGB(184, 134, 11) },
	{ "darkgray", NSVG_RGB(169, 169, 169) },
	{ "darkgreen", NSVG_RGB( 0, 100, 0) },
	{ "darkgrey", NSVG_RGB(169, 169, 169) },
	{ "darkkhaki", NSVG_RGB(189, 183, 107) },
	{ "darkmagenta", NSVG_RGB(139, 0, 139) },
	{ "darkolivegreen", NSVG_RGB( 85, 107, 47) },
	{ "darkorange", NSVG_RGB(255, 140, 0) },
	{ "darkorchid", NSVG_RGB(153, 50, 204) },
	{ "darkred", NSVG_RGB(139, 0, 0) },
	{ "darksalmon", NSVG_RGB(233, 150, 122) },
	{ "darkseagreen", NSVG_RGB(143, 188, 143) },
	{ "darkslateblue", NSVG_RGB( 72, 61, 139) },
	{ "darkslategray", NSVG_RGB( 47, 79, 79) },
	{ "darkslategrey", NSVG_RGB( 47, 79, 79) },
	{ "darkturquoise", NSVG_RGB( 0, 206, 209) },
	{ "darkviolet", NSVG_RGB(148, 0, 211) },
	{ "deeppink", NSVG_RGB(255, 20, 147) },
	{ "deepskyblue", NSVG_RGB( 0, 191, 255) },
	{ "dimgray", NSVG_RGB(105, 105, 105) },
	{ "dimgrey", NSVG_RGB(105, 105, 105) },
	{ "dodgerblue", NSVG_RGB( 30, 144, 255) },
	{ "firebrick", NSVG_RGB(178, 34, 34) },
	{ "floralwhite", NSVG_RGB(255, 250, 240) },
	{ "forestgreen", NSVG_RGB( 34, 139, 34) },
	{ "fuchsia", NSVG_RGB(255, 0, 255) },
	{ "gainsboro", NSVG_RGB(220, 220, 220) },
	{ "ghostwhite", NSVG_RGB(248, 248, 255) },
	{ "gold", NSVG_RGB(255, 215, 0) },
	{ "goldenrod", NSVG_RGB(218, 165, 32) },
	{ "greenyellow", NSVG_RGB(173, 255, 47) },
	{ "honeydew", NSVG_RGB(240, 255, 240) },
	{ "hotpink", NSVG_RGB(255, 105, 180) },
	{ "indianred", NSVG_RGB(205, 92, 92) },
	{ "indigo", NSVG_RGB( 75, 0, 130) },
	{ "ivory", NSVG_RGB(255, 255, 240) },
	{ "khaki", NSVG_RGB(240, 230, 140) },
	{ "lavender", NSVG_RGB(230, 230, 250) },
	{ "lavenderblush", NSVG_RGB(255, 240, 245) },
	{ "lawngreen", NSVG_RGB(124, 252, 0) },
	{ "lemonchiffon", NSVG_RGB(255, 250, 205) },
	{ "lightblue", NSVG_RGB(173, 216, 230) },
	{ "lightcoral", NSVG_RGB(240, 128, 128) },
	{ "lightcyan", NSVG_RGB(224, 255, 255) },
	{ "lightgoldenrodyellow", NSVG_RGB(250, 250, 210) },
	{ "lightgray", NSVG_RGB(211, 211, 211) },
	{ "lightgreen", NSVG_RGB(144, 238, 144) },
	{ "lightgrey", NSVG_RGB(211, 211, 211) },
	{ "lightpink", NSVG_RGB(255, 182, 193) },
	{ "lightsalmon", NSVG_RGB(255, 160, 122) },
	{ "lightseagreen", NSVG_RGB( 32, 178, 170) },
	{ "lightskyblue", NSVG_RGB(135, 206, 250) },
	{ "lightslategray", NSVG_RGB(119, 136, 153) },
	{ "lightslategrey", NSVG_RGB(119, 136, 153) },
	{ "lightsteelblue", NSVG_RGB(176, 196, 222) },
	{ "lightyellow", NSVG_RGB(255, 255, 224) },
	{ "lime", NSVG_RGB( 0, 255, 0) },
	{ "limegreen", NSVG_RGB( 50, 205, 50) },
	{ "linen", NSVG_RGB(250, 240, 230) },
	{ "maroon", NSVG_RGB(128, 0, 0) },
	{ "mediumaquamarine", NSVG_RGB(102, 205, 170) },
	{ "mediumblue", NSVG_RGB( 0, 0, 205) },
	{ "mediumorchid", NSVG_RGB(186, 85, 211) },
	{ "mediumpurple", NSVG_RGB(147, 112, 219) },
	{ "mediumseagreen", NSVG_RGB( 60, 179, 113) },
	{ "mediumslateblue", NSVG_RGB(123, 104, 238) },
	{ "mediumspringgreen", NSVG_RGB( 0, 250, 154) },
	{ "mediumturquoise", NSVG_RGB( 72, 209, 204) },
	{ "mediumvioletred", NSVG_RGB(199, 21, 133) },
	{ "midnightblue", NSVG_RGB( 25, 25, 112) },
	{ "mintcream", NSVG_RGB(245, 255, 250) },
	{ "mistyrose", NSVG_RGB(255, 228, 225) },
	{ "moccasin", NSVG_RGB(255, 228, 181) },
	{ "navajowhite", NSVG_RGB(255, 222, 173) },
	{ "navy", NSVG_RGB( 0, 0, 128) },
	{ "oldlace", NSVG_RGB(253, 245, 230) },
	{ "olive", NSVG_RGB(128, 128, 0) },
	{ "olivedrab", NSVG_RGB(107, 142, 35) },
	{ "orange", NSVG_RGB(255, 165, 0) },
	{ "orangered", NSVG_RGB(255, 69, 0) },
	{ "orchid", NSVG_RGB(218, 112, 214) },
	{ "palegoldenrod", NSVG_RGB(238, 232, 170) },
	{ "palegreen", NSVG_RGB(152, 251, 152) },
	{ "paleturquoise", NSVG_RGB(175, 238, 238) },
	{ "palevioletred", NSVG_RGB(219, 112, 147) },
	{ "papayawhip", NSVG_RGB(255, 239, 213) },
	{ "peachpuff", NSVG_RGB(255, 218, 185) },
	{ "peru", NSVG_RGB(205, 133, 63) },
	{ "pink", NSVG_RGB(255, 192, 203) },
	{ "plum", NSVG_RGB(221, 160, 221) },
	{ "powderblue", NSVG_RGB(176, 224, 230) },
	{ "purple", NSVG_RGB(128, 0, 128) },
	{ "rosybrown", NSVG_RGB(188, 143, 143) },
	{ "royalblue", NSVG_RGB( 65, 105, 225) },
	{ "saddlebrown", NSVG_RGB(139, 69, 19) },
	{ "salmon", NSVG_RGB(250, 128, 114) },
	{ "sandybrown", NSVG_RGB(244, 164, 96) },
	{ "seagreen", NSVG_RGB( 46, 139, 87) },
	{ "seashell", NSVG_RGB(255, 245, 238) },
	{ "sienna", NSVG_RGB(160, 82, 45) },
	{ "silver", NSVG_RGB(192, 192, 192) },
	{ "skyblue", NSVG_RGB(135, 206, 235) },
	{ "slateblue", NSVG_RGB(106, 90, 205) },
	{ "slategray", NSVG_RGB(112, 128, 144) },
	{ "slategrey", NSVG_RGB(112, 128, 144) },
	{ "snow", NSVG_RGB(255, 250, 250) },
	{ "springgreen", NSVG_RGB( 0, 255, 127) },
	{ "steelblue", NSVG_RGB( 70, 130, 180) },
	{ "tan", NSVG_RGB(210, 180, 140) },
	{ "teal", NSVG_RGB( 0, 128, 128) },
	{ "thistle", NSVG_RGB(216, 191, 216) },
	{ "tomato", NSVG_RGB(255, 99, 71) },
	{ "turquoise", NSVG_RGB( 64, 224, 208) },
	{ "violet", NSVG_RGB(238, 130, 238) },
	{ "wheat", NSVG_RGB(245, 222, 179) },
	{ "whitesmoke", NSVG_RGB(245, 245, 245) },
	{ "yellowgreen", NSVG_RGB(154, 205, 50) },
#endif
};

static unsigned int nsvg__parseColorName(const char* str)
{
	int i, ncolors = sizeof(nsvg__colors) / sizeof(NSVGNamedColor);

	for (i = 0; i < ncolors; i++) {
		if (strcmp(nsvg__colors[i].name, str) == 0) {
			return nsvg__colors[i].color;
		}
	}

	return NSVG_RGB(128, 128, 128);
}

static unsigned int nsvg__parseColor(const char* str)
{
	size_t len = 0;
	while(*str == ' ') ++str;
	len = strlen(str);
	if (len >= 1 && *str == '#')
		return nsvg__parseColorHex(str);
	else if (len >= 4 && str[0] == 'r' && str[1] == 'g' && str[2] == 'b' && str[3] == '(')
		return nsvg__parseColorRGB(str);
	return nsvg__parseColorName(str);
}

// Like sscanf %f, skipping leading white space
static float nsvg__parseFloat(const char* str)
{
	double value;
	while (*str && nsvg__isspace(*str)) str++;
	nsvg__parseNumberValue(str, &value, 1);
	return (float)value;
}

static float nsvg__parseOpacity(const char* str)
{
	float val = nsvg__parseFloat(str);
	if (val < 0.0f) val = 0.0f;
	if (val > 1.0f) val = 1.0f;
	return val;
}

static float nsvg__parseMiterLimit(const char* str)
{
	float val = nsvg__parseFloat(str);
	if (val < 0.0f) val = 0.0f;
	return val;
}

static int nsvg__parseUnits(const char* units)
{
	if (units[0] == 'p' && units[1] == 'x')
		return NSVG_UNITS_PX;
	else if (units[0] == 'p' && units[1] == 't')
		return NSVG_UNITS_PT;
	else if (units[0] == 'p' && units[1] == 'c')
		return NSVG_UNITS_PC;
	else if (units[0] == 'm' && units[1] == 'm')
		return NSVG_UNITS_MM;
	else if (units[0] == 'c' && units[1] == 'm')
		return NSVG_UNITS_CM;
	else if (units[0] == 'i' && units[1] == 'n')
		return NSVG_UNITS_IN;
	else if (units[0] == '%')
		return NSVG_UNITS_PERCENT;
	else if (units[0] == 'e' && units[1] == 'm')
		return NSVG_UNITS_EM;
	else if (units[0] == 'e' && units[1] == 'x')
		return NSVG_UNITS_EX;
	return NSVG_UNITS_USER;
}

static NSVGcoordinate nsvg__parseCoordinateRaw(const char* str)
{
	NSVGcoordinate coord = {0, NSVG_UNITS_USER};
	double value;
	const char* end;
	while (*str && nsvg__isspace(*str)) str++;
	end = nsvg__parseNumberValue(str, &value, 1);
	if (end != str) {
		coord.value = (float)value;
		while (*end && nsvg__isspace(*end)) end++;
		coord.units = nsvg__parseUnits(end);
	}
	return coord;
}

static NSVGcoordinate nsvg__coord(float v, int units)
{
	NSVGcoordinate coord = {v, units};
	return coord;
}

static float nsvg__parseCoordinate(NSVGparser* p, const char* str, float orig, float length)
{
	NSVGcoordinate coord = nsvg__parseCoordinateRaw(str);
	return nsvg__convertToPixels(p, coord, orig, length);
}

static int nsvg__parseTransformArgs(const char* str, float* args, int maxNa, int* na)
{
	const char* end;
	const char* ptr;

	*na = 0;
	memset(args, 0, sizeof(float) * maxNa);	// Missing args are 0, not uninitialised
	ptr = str;
	while (*ptr && *ptr != '(') ++ptr;
	if (*ptr == 0)
		return 1;
	end = ptr;
	while (*end && *end != ')') ++end;
	if (*end == 0)
		return 1;

	while (ptr < end) {
		if (*ptr == '-' || *ptr == '+' || *ptr == '.' || nsvg__isdigit(*ptr)) {
			if (*na >= maxNa) break;	// Ignore extra args, as returning 0 makes nsvg__parseTransform() loop forever
			double value;
			ptr = nsvg__parseNumberValue(ptr, &value, 0);
			args[(*na)++] = (float)value;
		} else {
			++ptr;
		}
	}
	return (int)(end - str);
}


static int nsvg__parseMatrix(float* xform, const char* str)
{
	float t[6];
	int na = 0;
	int len = nsvg__parseTransformArgs(str, t, 6, &na);
	if (na != 6) {
		nsvg__xformIdentity(xform);	// Not left uninitialised
		return len;
	}
	memcpy(xform, t, sizeof(float)*6);
	return len;
}

static int nsvg__parseTranslate(float* xform, const char* str)
{
	float args[2];
	float t[6];
	int na = 0;
	int len = nsvg__parseTransformArgs(str, args, 2, &na);
	if (na == 1) args[1] = 0.0;

	nsvg__xformSetTranslation(t, args[0], args[1]);
	memcpy(xform, t, sizeof(float)*6);
	return len;
}

static int nsvg__parseScale(float* xform, const char* str)
{
	float args[2];
	int na = 0;
	float t[6];
	int len = nsvg__parseTransformArgs(str, args, 2, &na);
	if (na == 1) args[1] = args[0];
	nsvg__xformSetScale(t, args[0], args[1]);
	memcpy(xform, t, sizeof(float)*6);
	return len;
}

static int nsvg__parseSkewX(float* xform, const char* str)
{
	float args[1];
	int na = 0;
	float t[6];
	int len = nsvg__parseTransformArgs(str, args, 1, &na);
	nsvg__xformSetSkewX(t, args[0]/180.0f*NSVG_PI);
	memcpy(xform, t, sizeof(float)*6);
	return len;
}

static int nsvg__parseSkewY(float* xform, const char* str)
{
	float args[1];
	int na = 0;
	float t[6];
	int len = nsvg__parseTransformArgs(str, args, 1, &na);
	nsvg__xformSetSkewY(t, args[0]/180.0f*NSVG_PI);
	memcpy(xform, t, sizeof(float)*6);
	return len;
}

static int nsvg__parseRotate(float* xform, const char* str)
{
	float args[3];
	int na = 0;
	float m[6];
	float t[6];
	int len = nsvg__parseTransformArgs(str, args, 3, &na);
	if (na == 1)
		args[1] = args[2] = 0.0f;
	nsvg__xformIdentity(m);

	if (na > 1) {
		nsvg__xformSetTranslation(t, -args[1], -args[2]);
		nsvg__xformMultiply(m, t);
	}

	nsvg__xformSetRotation(t, args[0]/180.0f*NSVG_PI);
	nsvg__xformMultiply(m, t);

	if (na > 1) {
		nsvg__xformSetTranslation(t, args[1], args[2]);
		nsvg__xformMultiply(m, t);
	}

	memcpy(xform, m, sizeof(float)*6);

	return len;
}

static void nsvg__parseTransform(float* xform, const char* str)
{
	float t[6];
	nsvg__xformIdentity(xform);
	while (*str)
	{
		if (strncmp(str, "matrix", 6) == 0)
			str += nsvg__parseMatrix(t, str);
		else if (strncmp(str, "translate", 9) == 0)
			str += nsvg__parseTranslate(t, str);
		else if (strncmp(str, "scale", 5) == 0)
			str += nsvg__parseScale(t, str);
		else if (strncmp(str, "rotate", 6) == 0)
			str += nsvg__parseRotate(t, str);
		else if (strncmp(str, "skewX", 5) == 0)
			str += nsvg__parseSkewX(t, str);
		else if (strncmp(str, "skewY", 5) == 0)
			str += nsvg__parseSkewY(t, str);
		else{
			++str;
			continue;
		}

		nsvg__xformPremultiply(xform, t);
	}
}

static void nsvg__parseUrl(char* id, const char* str)
{
	int i = 0;
	str += 4; // "url(";
	if (*str == '#')
		str++;
	while (i < 63 && *str != ')') {
		id[i] = *str++;
		i++;
	}
	id[i] = '\0';
}

static char nsvg__parseLineCap(const char* str)
{
	if (strcmp(str, "butt") == 0)
		return NSVG_CAP_BUTT;
	else if (strcmp(str, "round") == 0)
		return NSVG_CAP_ROUND;
	else if (strcmp(str, "square") == 0)
		return NSVG_CAP_SQUARE;
	// TODO: handle inherit.
	return NSVG_CAP_BUTT;
}

static char nsvg__parseLineJoin(const char* str)
{
	if (strcmp(str, "miter") == 0)
		return NSVG_JOIN_MITER;
	else if (strcmp(str, "round") == 0)
		return NSVG_JOIN_ROUND;
	else if (strcmp(str, "bevel") == 0)
		return NSVG_JOIN_BEVEL;
	// TODO: handle inherit.
	return NSVG_JOIN_MITER;
}

static char nsvg__parseFillRule(const char* str)
{
	if (strcmp(str, "nonzero") == 0)
		return NSVG_FILLRULE_NONZERO;
	else if (strcmp(str, "evenodd") == 0)
		return NSVG_FILLRULE_EVENODD;
	// TODO: handle inherit.
	return NSVG_FILLRULE_NONZERO;
}

static const char* nsvg__getNextDashItem(const char* s, char* it)
{
	int n = 0;
	it[0] = '\0';
	// Skip white spaces and commas
	while (*s && (nsvg__isspace(*s) || *s == ',')) s++;
	// Advance until whitespace, comma or end.
	while (*s && (!nsvg__isspace(*s) && *s != ',')) {
		if (n < 63)
			it[n++] = *s;
		s++;
	}
	it[n++] = '\0';
	return s;
}

static int nsvg__parseStrokeDashArray(NSVGparser* p, const char* str, float* strokeDashArray)
{
	char item[64];
	int count = 0, i;
	float sum = 0.0f;

	// Handle "none"
	if (str[0] == 'n')
		return 0;

	// Parse dashes
	while (*str) {
		str = nsvg__getNextDashItem(str, item);
		if (!*item) break;
		if (count < NSVG_MAX_DASHES)
			strokeDashArray[count++] = fabsf(nsvg__parseCoordinate(p, item, 0.0f, nsvg__actualLength(p)));
	}

	for (i = 0; i < count; i++)
		sum += strokeDashArray[i];
	if (sum <= 1e-6f)
		count = 0;

	return count;
}

static void nsvg__parseStyle(NSVGparser* p, const char* str);

// strcmp() == 0, testing the first characters inline as most attribute names differ there
static NSVG_INLINE int nsvg__streq(const char* a, const char* b)
{
	return a[0] == b[0] && strcmp(a, b) == 0;
}

static int nsvg__parseAttr(NSVGparser* p, const char* name, const char* value)
{
	float xform[6];
	NSVGattrib* attr = nsvg__getAttr(p);
	if (!attr) return 0;

	if (nsvg__streq(name, "style")) {
		nsvg__parseStyle(p, value);
	} else if (nsvg__streq(name, "display")) {
		if (strcmp(value, "none") == 0)
			attr->visible = 0;
		// Don't reset ->visible on display:inline, one display:none hides the whole subtree

	} else if (nsvg__streq(name, "fill")) {
		if (strcmp(value, "none") == 0) {
			attr->hasFill = 0;
		} else if (strncmp(value, "url(", 4) == 0) {
			attr->hasFill = 2;
			nsvg__parseUrl(attr->fillGradient, value);
		} else {
			attr->hasFill = 1;
			attr->fillColor = nsvg__parseColor(value);
		}
	} else if (nsvg__streq(name, "opacity")) {
		attr->opacity *= nsvg__parseOpacity(value);
	} else if (nsvg__streq(name, "fill-opacity")) {
		attr->fillOpacity = nsvg__parseOpacity(value);
	} else if (nsvg__streq(name, "stroke")) {
		if (strcmp(value, "none") == 0) {
			attr->hasStroke = 0;
		} else if (strncmp(value, "url(", 4) == 0) {
			attr->hasStroke = 2;
			nsvg__parseUrl(attr->strokeGradient, value);
		} else {
			attr->hasStroke = 1;
			attr->strokeColor = nsvg__parseColor(value);
		}
	} else if (nsvg__streq(name, "stroke-width")) {
		attr->strokeWidth = nsvg__parseCoordinate(p, value, 0.0f, nsvg__actualLength(p));
	} else if (nsvg__streq(name, "stroke-dasharray")) {
		attr->strokeDashCount = nsvg__parseStrokeDashArray(p, value, attr->strokeDashArray);
	} else if (nsvg__streq(name, "stroke-dashoffset")) {
		attr->strokeDashOffset = nsvg__parseCoordinate(p, value, 0.0f, nsvg__actualLength(p));
	} else if (nsvg__streq(name, "stroke-opacity")) {
		attr->strokeOpacity = nsvg__parseOpacity(value);
	} else if (nsvg__streq(name, "stroke-linecap")) {
		attr->strokeLineCap = nsvg__parseLineCap(value);
	} else if (nsvg__streq(name, "stroke-linejoin")) {
		attr->strokeLineJoin = nsvg__parseLineJoin(value);
	} else if (nsvg__streq(name, "stroke-miterlimit")) {
		attr->miterLimit = nsvg__parseMiterLimit(value);
	} else if (nsvg__streq(name, "fill-rule")) {
		attr->fillRule = nsvg__parseFillRule(value);
	} else if (nsvg__streq(name, "clip-rule")) {
		attr->clipRule = nsvg__parseFillRule(value);
	} else if (nsvg__streq(name, "clip-path")) {
		if (strncmp(value, "url(", 4) == 0)
			nsvg__parseUrl(attr->clipPathId, value);
		else
			attr->clipPathId[0] = '\0';
	} else if (nsvg__streq(name, "font-size")) {
		attr->fontSize = nsvg__parseCoordinate(p, value, 0.0f, nsvg__actualLength(p));
	} else if (nsvg__streq(name, "transform")) {
		nsvg__parseTransform(xform, value);
		nsvg__xformPremultiply(attr->xform, xform);
	} else if (nsvg__streq(name, "stop-color")) {
		attr->stopColor = nsvg__parseColor(value);
	} else if (nsvg__streq(name, "stop-opacity")) {
		attr->stopOpacity = nsvg__parseOpacity(value);
	} else if (nsvg__streq(name, "offset")) {
		attr->stopOffset = nsvg__parseCoordinate(p, value, 0.0f, 1.0f);
	} else if (nsvg__streq(name, "id")) {
		strncpy(attr->id, value, 63);
		attr->id[63] = '\0';
	} else if (nsvg__streq(name, "x")) {
		nsvg__xformSetTranslation(xform, (float)nsvg__atof(value), 0);
		nsvg__xformPremultiply(attr->xform, xform);
	} else if (nsvg__streq(name, "y")) {
		nsvg__xformSetTranslation(xform, 0, (float)nsvg__atof(value));
		nsvg__xformPremultiply(attr->xform, xform);
	} else {
		return 0;
	}
	return 1;
}

static int nsvg__parseNameValue(NSVGparser* p, const char* start, const char* end)
{
	const char* str;
	const char* val;
	char name[512];
	char value[512];
	int n;

	str = start;
	while (str < end && *str != ':') ++str;

	val = str;

	// Right Trim
	while (str > start &&  (*str == ':' || nsvg__isspace(*str))) --str;
	++str;

	n = (int)(str - start);
	if (n > 511) n = 511;
	if (n) memcpy(name, start, n);
	name[n] = 0;

	while (val < end && (*val == ':' || nsvg__isspace(*val))) ++val;

	n = (int)(end - val);
	if (n > 511) n = 511;
	if (n) memcpy(value, val, n);
	value[n] = 0;

	return nsvg__parseAttr(p, name, value);
}

static void nsvg__parseStyle(NSVGparser* p, const char* str)
{
	const char* start;
	const char* end;

	while (*str) {
		// Left Trim
		while(*str && nsvg__isspace(*str)) ++str;
		start = str;
		while(*str && *str != ';') ++str;
		end = str;

		// Right Trim
		while (end > start &&  (*end == ';' || nsvg__isspace(*end))) --end;
		++end;

		nsvg__parseNameValue(p, start, end);
		if (*str) ++str;
	}
}

static void nsvg__parseAttribs(NSVGparser* p, const char** attr)
{
	int i;
	for (i = 0; attr[i]; i += 2)
	{
		if (strcmp(attr[i], "style") == 0)
			nsvg__parseStyle(p, attr[i + 1]);
		else
			nsvg__parseAttr(p, attr[i], attr[i + 1]);
	}
}

static int nsvg__getArgsPerElement(char cmd)
{
	switch (cmd) {
		case 'v':
		case 'V':
		case 'h':
		case 'H':
			return 1;
		case 'm':
		case 'M':
		case 'l':
		case 'L':
		case 't':
		case 'T':
			return 2;
		case 'q':
		case 'Q':
		case 's':
		case 'S':
			return 4;
		case 'c':
		case 'C':
			return 6;
		case 'a':
		case 'A':
			return 7;
	}
	return 0;
}

static void nsvg__pathMoveTo(NSVGparser* p, float* cpx, float* cpy, float* args, int rel)
{
	if (rel) {
		*cpx += args[0];
		*cpy += args[1];
	} else {
		*cpx = args[0];
		*cpy = args[1];
	}
	nsvg__moveTo(p, *cpx, *cpy);
}

static void nsvg__pathLineTo(NSVGparser* p, float* cpx, float* cpy, float* args, int rel)
{
	if (rel) {
		*cpx += args[0];
		*cpy += args[1];
	} else {
		*cpx = args[0];
		*cpy = args[1];
	}
	nsvg__lineTo(p, *cpx, *cpy);
}

static void nsvg__pathHLineTo(NSVGparser* p, float* cpx, float* cpy, float* args, int rel)
{
	if (rel)
		*cpx += args[0];
	else
		*cpx = args[0];
	nsvg__lineTo(p, *cpx, *cpy);
}

static void nsvg__pathVLineTo(NSVGparser* p, float* cpx, float* cpy, float* args, int rel)
{
	if (rel)
		*cpy += args[0];
	else
		*cpy = args[0];
	nsvg__lineTo(p, *cpx, *cpy);
}

static void nsvg__pathCubicBezTo(NSVGparser* p, float* cpx, float* cpy,
								 float* cpx2, float* cpy2, float* args, int rel)
{
	float x2, y2, cx1, cy1, cx2, cy2;

	if (rel) {
		cx1 = *cpx + args[0];
		cy1 = *cpy + args[1];
		cx2 = *cpx + args[2];
		cy2 = *cpy + args[3];
		x2 = *cpx + args[4];
		y2 = *cpy + args[5];
	} else {
		cx1 = args[0];
		cy1 = args[1];
		cx2 = args[2];
		cy2 = args[3];
		x2 = args[4];
		y2 = args[5];
	}

	nsvg__cubicBezTo(p, cx1,cy1, cx2,cy2, x2,y2);

	*cpx2 = cx2;
	*cpy2 = cy2;
	*cpx = x2;
	*cpy = y2;
}

static void nsvg__pathCubicBezShortTo(NSVGparser* p, float* cpx, float* cpy,
									  float* cpx2, float* cpy2, float* args, int rel)
{
	float x1, y1, x2, y2, cx1, cy1, cx2, cy2;

	x1 = *cpx;
	y1 = *cpy;
	if (rel) {
		cx2 = *cpx + args[0];
		cy2 = *cpy + args[1];
		x2 = *cpx + args[2];
		y2 = *cpy + args[3];
	} else {
		cx2 = args[0];
		cy2 = args[1];
		x2 = args[2];
		y2 = args[3];
	}

	cx1 = 2*x1 - *cpx2;
	cy1 = 2*y1 - *cpy2;

	nsvg__cubicBezTo(p, cx1,cy1, cx2,cy2, x2,y2);

	*cpx2 = cx2;
	*cpy2 = cy2;
	*cpx = x2;
	*cpy = y2;
}

static void nsvg__pathQuadBezTo(NSVGparser* p, float* cpx, float* cpy,
								float* cpx2, float* cpy2, float* args, int rel)
{
	float x1, y1, x2, y2, cx, cy;
	float cx1, cy1, cx2, cy2;

	x1 = *cpx;
	y1 = *cpy;
	if (rel) {
		cx = *cpx + args[0];
		cy = *cpy + args[1];
		x2 = *cpx + args[2];
		y2 = *cpy + args[3];
	} else {
		cx = args[0];
		cy = args[1];
		x2 = args[2];
		y2 = args[3];
	}

	// Convert to cubic bezier
	cx1 = x1 + 2.0f/3.0f*(cx - x1);
	cy1 = y1 + 2.0f/3.0f*(cy - y1);
	cx2 = x2 + 2.0f/3.0f*(cx - x2);
	cy2 = y2 + 2.0f/3.0f*(cy - y2);

	nsvg__cubicBezTo(p, cx1,cy1, cx2,cy2, x2,y2);

	*cpx2 = cx;
	*cpy2 = cy;
	*cpx = x2;
	*cpy = y2;
}

static void nsvg__pathQuadBezShortTo(NSVGparser* p, float* cpx, float* cpy,
									 float* cpx2, float* cpy2, float* args, int rel)
{
	float x1, y1, x2, y2, cx, cy;
	float cx1, cy1, cx2, cy2;

	x1 = *cpx;
	y1 = *cpy;
	if (rel) {
		x2 = *cpx + args[0];
		y2 = *cpy + args[1];
	} else {
		x2 = args[0];
		y2 = args[1];
	}

	cx = 2*x1 - *cpx2;
	cy = 2*y1 - *cpy2;

	// Convert to cubix bezier
	cx1 = x1 + 2.0f/3.0f*(cx - x1);
	cy1 = y1 + 2.0f/3.0f*(cy - y1);
	cx2 = x2 + 2.0f/3.0f*(cx - x2);
	cy2 = y2 + 2.0f/3.0f*(cy - y2);

	nsvg__cubicBezTo(p, cx1,cy1, cx2,cy2, x2,y2);

	*cpx2 = cx;
	*cpy2 = cy;
	*cpx = x2;
	*cpy = y2;
}

static float nsvg__sqr(float x) { return x*x; }
static float nsvg__vmag(float x, float y) { return sqrtf(x*x + y*y); }

static float nsvg__vecrat(float ux, float uy, float vx, float vy)
{
	return (ux*vx + uy*vy) / (nsvg__vmag(ux,uy) * nsvg__vmag(vx,vy));
}

static float nsvg__vecang(float ux, float uy, float vx, float vy)
{
	float r = nsvg__vecrat(ux,uy, vx,vy);
	if (r < -1.0f) r = -1.0f;
	if (r > 1.0f) r = 1.0f;
	return ((ux*vy < uy*vx) ? -1.0f : 1.0f) * acosf(r);
}

static void nsvg__pathArcTo(NSVGparser* p, float* cpx, float* cpy, float* args, int rel)
{
	// Ported from canvg (https://code.google.com/p/canvg/)
	float rx, ry, rotx;
	float x1, y1, x2, y2, cx, cy, dx, dy, d;
	float x1p, y1p, cxp, cyp, s, sa, sb;
	float ux, uy, vx, vy, a1, da;
	float x, y, tanx, tany, a, px = 0, py = 0, ptanx = 0, ptany = 0, t[6];
	float sinrx, cosrx;
	int fa, fs;
	int i, ndivs;
	float hda, kappa;

	rx = fabsf(args[0]);				// y radius
	ry = fabsf(args[1]);				// x radius
	rotx = args[2] / 180.0f * NSVG_PI;		// x rotation angle
	fa = fabsf(args[3]) > 1e-6 ? 1 : 0;	// Large arc
	fs = fabsf(args[4]) > 1e-6 ? 1 : 0;	// Sweep direction
	x1 = *cpx;							// start point
	y1 = *cpy;
	if (rel) {							// end point
		x2 = *cpx + args[5];
		y2 = *cpy + args[6];
	} else {
		x2 = args[5];
		y2 = args[6];
	}

	dx = x1 - x2;
	dy = y1 - y2;
	d = sqrtf(dx*dx + dy*dy);
	if (d < 1e-6f || rx < 1e-6f || ry < 1e-6f) {
		// The arc degenerates to a line
		nsvg__lineTo(p, x2, y2);
		*cpx = x2;
		*cpy = y2;
		return;
	}

	sinrx = sinf(rotx);
	cosrx = cosf(rotx);

	// Convert to center point parameterization.
	// http://www.w3.org/TR/SVG11/implnote.html#ArcImplementationNotes
	// 1) Compute x1', y1'
	x1p = cosrx * dx / 2.0f + sinrx * dy / 2.0f;
	y1p = -sinrx * dx / 2.0f + cosrx * dy / 2.0f;
	d = nsvg__sqr(x1p)/nsvg__sqr(rx) + nsvg__sqr(y1p)/nsvg__sqr(ry);
	if (d > 1) {
		d = sqrtf(d);
		rx *= d;
		ry *= d;
	}
	// 2) Compute cx', cy'
	s = 0.0f;
	sa = nsvg__sqr(rx)*nsvg__sqr(ry) - nsvg__sqr(rx)*nsvg__sqr(y1p) - nsvg__sqr(ry)*nsvg__sqr(x1p);
	sb = nsvg__sqr(rx)*nsvg__sqr(y1p) + nsvg__sqr(ry)*nsvg__sqr(x1p);
	if (sa < 0.0f) sa = 0.0f;
	if (sb > 0.0f)
		s = sqrtf(sa / sb);
	if (fa == fs)
		s = -s;
	cxp = s * rx * y1p / ry;
	cyp = s * -ry * x1p / rx;

	// 3) Compute cx,cy from cx',cy'
	cx = (x1 + x2)/2.0f + cosrx*cxp - sinrx*cyp;
	cy = (y1 + y2)/2.0f + sinrx*cxp + cosrx*cyp;

	// 4) Calculate theta1, and delta theta.
	ux = (x1p - cxp) / rx;
	uy = (y1p - cyp) / ry;
	vx = (-x1p - cxp) / rx;
	vy = (-y1p - cyp) / ry;
	a1 = nsvg__vecang(1.0f,0.0f, ux,uy);	// Initial angle
	da = nsvg__vecang(ux,uy, vx,vy);		// Delta angle

//	if (vecrat(ux,uy,vx,vy) <= -1.0f) da = NSVG_PI;
//	if (vecrat(ux,uy,vx,vy) >= 1.0f) da = 0;

	if (fs == 0 && da > 0)
		da -= 2 * NSVG_PI;
	else if (fs == 1 && da < 0)
		da += 2 * NSVG_PI;

    // A large arc sweeps at least half a turn. Near-closed arcs (end point within float precision of the start, as
    // Inkscape writes ellipses) lose that to rounding, leaving da near zero, so take the long way round.
    if (fa && fabsf(da) < NSVG_PI)
        da = (fs ? 1.0f : -1.0f) * (2 * NSVG_PI - fabsf(da));
    
	// Approximate the arc using cubic spline segments.
	t[0] = cosrx; t[1] = sinrx;
	t[2] = -sinrx; t[3] = cosrx;
	t[4] = cx; t[5] = cy;

	// Split arc into max 90 degree segments.
	// The loop assumes an iteration per end point (including start and end), this +1.
	ndivs = (int)(fabsf(da) / (NSVG_PI*0.5f) + 1.0f);
	hda = (da / (float)ndivs) / 2.0f;
    kappa = hda == 0.0f ? 0.0f : fabsf(4.0f / 3.0f * (1.0f - cosf(hda)) / sinf(hda));
	if (da < 0.0f)
		kappa = -kappa;

	for (i = 0; i <= ndivs; i++) {
		a = a1 + da * ((float)i/(float)ndivs);
		dx = cosf(a);
		dy = sinf(a);
		nsvg__xformPoint(&x, &y, dx*rx, dy*ry, t); // position
		nsvg__xformVec(&tanx, &tany, -dy*rx * kappa, dx*ry * kappa, t); // tangent
		if (i > 0)
			nsvg__cubicBezTo(p, px+ptanx,py+ptany, x-tanx, y-tany, x, y);
		px = x;
		py = y;
		ptanx = tanx;
		ptany = tany;
	}

	*cpx = x2;
	*cpy = y2;
}

static void nsvg__parsePath(NSVGparser* p, const char** attr)
{
	const char* s = NULL;
	char cmd = '\0';
	float args[10];
	int nargs;
	int rargs = 0;
	float cpx, cpy, cpx2, cpy2, value;
	const char* tmp[4];
	char closedFlag;
	int i;
	char item[64];

	for (i = 0; attr[i]; i += 2) {
		if (strcmp(attr[i], "d") == 0) {
			s = attr[i + 1];
		} else {
			tmp[0] = attr[i];
			tmp[1] = attr[i + 1];
			tmp[2] = 0;
			tmp[3] = 0;
			nsvg__parseAttribs(p, tmp);
		}
	}

	if (s) {
		nsvg__resetPath(p);
		cpx = 0; cpy = 0;
		cpx2 = 0; cpy2 = 0;
		closedFlag = 0;
		nargs = 0;

		while (*s) {
			s = nsvg__getNextPathItem(s, item, &value);
			if (!*item) break;
			if (nsvg__isnum(item[0])) {
				if (nargs < 10)
					args[nargs++] = value;
				if (nargs >= rargs) {
					switch (cmd) {
						case 'm':
						case 'M':
							nsvg__pathMoveTo(p, &cpx, &cpy, args, cmd == 'm' ? 1 : 0);
							// Moveto can be followed by multiple coordinate pairs,
							// which should be treated as linetos.
							cmd = (cmd == 'm') ? 'l' : 'L';
							rargs = nsvg__getArgsPerElement(cmd);
							cpx2 = cpx; cpy2 = cpy;
							break;
						case 'l':
						case 'L':
							nsvg__pathLineTo(p, &cpx, &cpy, args, cmd == 'l' ? 1 : 0);
							cpx2 = cpx; cpy2 = cpy;
							break;
						case 'H':
						case 'h':
							nsvg__pathHLineTo(p, &cpx, &cpy, args, cmd == 'h' ? 1 : 0);
							cpx2 = cpx; cpy2 = cpy;
							break;
						case 'V':
						case 'v':
							nsvg__pathVLineTo(p, &cpx, &cpy, args, cmd == 'v' ? 1 : 0);
							cpx2 = cpx; cpy2 = cpy;
							break;
						case 'C':
						case 'c':
							nsvg__pathCubicBezTo(p, &cpx, &cpy, &cpx2, &cpy2, args, cmd == 'c' ? 1 : 0);
							break;
						case 'S':
						case 's':
							nsvg__pathCubicBezShortTo(p, &cpx, &cpy, &cpx2, &cpy2, args, cmd == 's' ? 1 : 0);
							break;
						case 'Q':
						case 'q':
							nsvg__pathQuadBezTo(p, &cpx, &cpy, &cpx2, &cpy2, args, cmd == 'q' ? 1 : 0);
							break;
						case 'T':
						case 't':
							nsvg__pathQuadBezShortTo(p, &cpx, &cpy, &cpx2, &cpy2, args, cmd == 't' ? 1 : 0);
							break;
						case 'A':
						case 'a':
							nsvg__pathArcTo(p, &cpx, &cpy, args, cmd == 'a' ? 1 : 0);
							cpx2 = cpx; cpy2 = cpy;
							break;
						default:
							if (nargs >= 2) {
								cpx = args[nargs-2];
								cpy = args[nargs-1];
								cpx2 = cpx; cpy2 = cpy;
							}
							break;
					}
					nargs = 0;
				}
			} else {
				cmd = item[0];
				rargs = nsvg__getArgsPerElement(cmd);
				if (cmd == 'M' || cmd == 'm') {
					// Commit path.
					if (p->npts > 0)
						nsvg__addPath(p, closedFlag);
					// Start new subpath.
					nsvg__resetPath(p);
					closedFlag = 0;
					nargs = 0;
				} else if (cmd == 'Z' || cmd == 'z') {
					closedFlag = 1;
					// Commit path.
					if (p->npts > 0) {
						// Move current point to first point
						cpx = p->pts[0];
						cpy = p->pts[1];
						cpx2 = cpx; cpy2 = cpy;
						nsvg__addPath(p, closedFlag);
					}
					// Start new subpath.
					nsvg__resetPath(p);
					nsvg__moveTo(p, cpx, cpy);
					closedFlag = 0;
					nargs = 0;
				}
			}
		}
		// Commit path.
		if (p->npts)
			nsvg__addPath(p, closedFlag);
	}

	nsvg__addShape(p);
}

static void nsvg__parseRect(NSVGparser* p, const char** attr)
{
	float x = 0.0f;
	float y = 0.0f;
	float w = 0.0f;
	float h = 0.0f;
	float rx = -1.0f; // marks not set
	float ry = -1.0f;
	int i;

	for (i = 0; attr[i]; i += 2) {
        if (strcmp(attr[i], "x") == 0) x = nsvg__parseCoordinate(p, attr[i+1], nsvg__actualOrigX(p), nsvg__actualWidth(p));
        else if (strcmp(attr[i], "y") == 0) y = nsvg__parseCoordinate(p, attr[i+1], nsvg__actualOrigY(p), nsvg__actualHeight(p));
        else if (strcmp(attr[i], "width") == 0) w = nsvg__parseCoordinate(p, attr[i+1], 0.0f, nsvg__actualWidth(p));
        else if (strcmp(attr[i], "height") == 0) h = nsvg__parseCoordinate(p, attr[i+1], 0.0f, nsvg__actualHeight(p));
        else if (strcmp(attr[i], "rx") == 0) rx = fabsf(nsvg__parseCoordinate(p, attr[i+1], 0.0f, nsvg__actualWidth(p)));
        else if (strcmp(attr[i], "ry") == 0) ry = fabsf(nsvg__parseCoordinate(p, attr[i+1], 0.0f, nsvg__actualHeight(p)));
        else nsvg__parseAttr(p, attr[i], attr[i + 1]);
	}

	if (rx < 0.0f && ry > 0.0f) rx = ry;
	if (ry < 0.0f && rx > 0.0f) ry = rx;
	if (rx < 0.0f) rx = 0.0f;
	if (ry < 0.0f) ry = 0.0f;
	if (rx > w/2.0f) rx = w/2.0f;
	if (ry > h/2.0f) ry = h/2.0f;

	if (w != 0.0f && h != 0.0f) {
		nsvg__resetPath(p);

		if (rx < 0.00001f || ry < 0.0001f) {
			nsvg__moveTo(p, x, y);
			nsvg__lineTo(p, x+w, y);
			nsvg__lineTo(p, x+w, y+h);
			nsvg__lineTo(p, x, y+h);
		} else {
			// Rounded rectangle
			nsvg__moveTo(p, x+rx, y);
			nsvg__lineTo(p, x+w-rx, y);
			nsvg__cubicBezTo(p, x+w-rx*(1-NSVG_KAPPA90), y, x+w, y+ry*(1-NSVG_KAPPA90), x+w, y+ry);
			nsvg__lineTo(p, x+w, y+h-ry);
			nsvg__cubicBezTo(p, x+w, y+h-ry*(1-NSVG_KAPPA90), x+w-rx*(1-NSVG_KAPPA90), y+h, x+w-rx, y+h);
			nsvg__lineTo(p, x+rx, y+h);
			nsvg__cubicBezTo(p, x+rx*(1-NSVG_KAPPA90), y+h, x, y+h-ry*(1-NSVG_KAPPA90), x, y+h-ry);
			nsvg__lineTo(p, x, y+ry);
			nsvg__cubicBezTo(p, x, y+ry*(1-NSVG_KAPPA90), x+rx*(1-NSVG_KAPPA90), y, x+rx, y);
		}

		nsvg__addPath(p, 1);

		nsvg__addShape(p);
	}
}

static void nsvg__parseCircle(NSVGparser* p, const char** attr)
{
	float cx = 0.0f;
	float cy = 0.0f;
	float r = 0.0f;
	int i;

	for (i = 0; attr[i]; i += 2) {
		if (!nsvg__parseAttr(p, attr[i], attr[i + 1])) {
			if (strcmp(attr[i], "cx") == 0) cx = nsvg__parseCoordinate(p, attr[i+1], nsvg__actualOrigX(p), nsvg__actualWidth(p));
			if (strcmp(attr[i], "cy") == 0) cy = nsvg__parseCoordinate(p, attr[i+1], nsvg__actualOrigY(p), nsvg__actualHeight(p));
			if (strcmp(attr[i], "r") == 0) r = fabsf(nsvg__parseCoordinate(p, attr[i+1], 0.0f, nsvg__actualLength(p)));
		}
	}

	if (r > 0.0f) {
		nsvg__resetPath(p);

		nsvg__moveTo(p, cx+r, cy);
		nsvg__cubicBezTo(p, cx+r, cy+r*NSVG_KAPPA90, cx+r*NSVG_KAPPA90, cy+r, cx, cy+r);
		nsvg__cubicBezTo(p, cx-r*NSVG_KAPPA90, cy+r, cx-r, cy+r*NSVG_KAPPA90, cx-r, cy);
		nsvg__cubicBezTo(p, cx-r, cy-r*NSVG_KAPPA90, cx-r*NSVG_KAPPA90, cy-r, cx, cy-r);
		nsvg__cubicBezTo(p, cx+r*NSVG_KAPPA90, cy-r, cx+r, cy-r*NSVG_KAPPA90, cx+r, cy);

		nsvg__addPath(p, 1);

		nsvg__addShape(p);
	}
}

static void nsvg__parseEllipse(NSVGparser* p, const char** attr)
{
	float cx = 0.0f;
	float cy = 0.0f;
	float rx = 0.0f;
	float ry = 0.0f;
	int i;

	for (i = 0; attr[i]; i += 2) {
		if (!nsvg__parseAttr(p, attr[i], attr[i + 1])) {
			if (strcmp(attr[i], "cx") == 0) cx = nsvg__parseCoordinate(p, attr[i+1], nsvg__actualOrigX(p), nsvg__actualWidth(p));
			if (strcmp(attr[i], "cy") == 0) cy = nsvg__parseCoordinate(p, attr[i+1], nsvg__actualOrigY(p), nsvg__actualHeight(p));
			if (strcmp(attr[i], "rx") == 0) rx = fabsf(nsvg__parseCoordinate(p, attr[i+1], 0.0f, nsvg__actualWidth(p)));
			if (strcmp(attr[i], "ry") == 0) ry = fabsf(nsvg__parseCoordinate(p, attr[i+1], 0.0f, nsvg__actualHeight(p)));
		}
	}

	if (rx > 0.0f && ry > 0.0f) {

		nsvg__resetPath(p);

		nsvg__moveTo(p, cx+rx, cy);
		nsvg__cubicBezTo(p, cx+rx, cy+ry*NSVG_KAPPA90, cx+rx*NSVG_KAPPA90, cy+ry, cx, cy+ry);
		nsvg__cubicBezTo(p, cx-rx*NSVG_KAPPA90, cy+ry, cx-rx, cy+ry*NSVG_KAPPA90, cx-rx, cy);
		nsvg__cubicBezTo(p, cx-rx, cy-ry*NSVG_KAPPA90, cx-rx*NSVG_KAPPA90, cy-ry, cx, cy-ry);
		nsvg__cubicBezTo(p, cx+rx*NSVG_KAPPA90, cy-ry, cx+rx, cy-ry*NSVG_KAPPA90, cx+rx, cy);

		nsvg__addPath(p, 1);

		nsvg__addShape(p);
	}
}

static void nsvg__parseLine(NSVGparser* p, const char** attr)
{
	float x1 = 0.0;
	float y1 = 0.0;
	float x2 = 0.0;
	float y2 = 0.0;
	int i;

	for (i = 0; attr[i]; i += 2) {
		if (!nsvg__parseAttr(p, attr[i], attr[i + 1])) {
			if (strcmp(attr[i], "x1") == 0) x1 = nsvg__parseCoordinate(p, attr[i + 1], nsvg__actualOrigX(p), nsvg__actualWidth(p));
			if (strcmp(attr[i], "y1") == 0) y1 = nsvg__parseCoordinate(p, attr[i + 1], nsvg__actualOrigY(p), nsvg__actualHeight(p));
			if (strcmp(attr[i], "x2") == 0) x2 = nsvg__parseCoordinate(p, attr[i + 1], nsvg__actualOrigX(p), nsvg__actualWidth(p));
			if (strcmp(attr[i], "y2") == 0) y2 = nsvg__parseCoordinate(p, attr[i + 1], nsvg__actualOrigY(p), nsvg__actualHeight(p));
		}
	}

	nsvg__resetPath(p);

	nsvg__moveTo(p, x1, y1);
	nsvg__lineTo(p, x2, y2);

	nsvg__addPath(p, 0);

	nsvg__addShape(p);
}

static void nsvg__parsePoly(NSVGparser* p, const char** attr, int closeFlag)
{
	int i;
	const char* s;
	float args[2], value;
	int nargs, npts = 0;
	char item[64];

	nsvg__resetPath(p);

	for (i = 0; attr[i]; i += 2) {
		if (!nsvg__parseAttr(p, attr[i], attr[i + 1])) {
			if (strcmp(attr[i], "points") == 0) {
				s = attr[i + 1];
				nargs = 0;
				while (*s) {
					s = nsvg__getNextPathItem(s, item, &value);
					args[nargs++] = value;
					if (nargs >= 2) {
						if (npts == 0)
							nsvg__moveTo(p, args[0], args[1]);
						else
							nsvg__lineTo(p, args[0], args[1]);
						nargs = 0;
						npts++;
					}
				}
			}
		}
	}

	nsvg__addPath(p, (char)closeFlag);

	nsvg__addShape(p);
}

// A preserveAspectRatio: none leaves the alignments, & meet is the default
static void nsvg__parseAspectRatio(const char* str, int* alignX, int* alignY, int* alignType)
{
	if (strstr(str, "none") != 0) {
		// No uniform scaling
		*alignType = NSVG_ALIGN_NONE;
	} else {
		// Parse X align
		if (strstr(str, "xMin") != 0)
			*alignX = NSVG_ALIGN_MIN;
		else if (strstr(str, "xMid") != 0)
			*alignX = NSVG_ALIGN_MID;
		else if (strstr(str, "xMax") != 0)
			*alignX = NSVG_ALIGN_MAX;
		// Parse Y align
		if (strstr(str, "yMin") != 0)
			*alignY = NSVG_ALIGN_MIN;
		else if (strstr(str, "yMid") != 0)
			*alignY = NSVG_ALIGN_MID;
		else if (strstr(str, "yMax") != 0)
			*alignY = NSVG_ALIGN_MAX;
		// Parse meet/slice
		*alignType = NSVG_ALIGN_MEET;
		if (strstr(str, "slice") != 0)
			*alignType = NSVG_ALIGN_SLICE;
	}
}

static void nsvg__parseSVG(NSVGparser* p, const char** attr)
{
	int i;
	for (i = 0; attr[i]; i += 2) {
		if (!nsvg__parseAttr(p, attr[i], attr[i + 1])) {
			if (strcmp(attr[i], "width") == 0) {
				p->image->width = nsvg__parseCoordinate(p, attr[i + 1], 0.0f, 0.0f);
			} else if (strcmp(attr[i], "height") == 0) {
				p->image->height = nsvg__parseCoordinate(p, attr[i + 1], 0.0f, 0.0f);
			} else if (strcmp(attr[i], "viewBox") == 0) {
				sscanf(attr[i + 1], "%f%*[%%, \t]%f%*[%%, \t]%f%*[%%, \t]%f", &p->viewMinx, &p->viewMiny, &p->viewWidth, &p->viewHeight);
			} else if (strcmp(attr[i], "preserveAspectRatio") == 0) {
				nsvg__parseAspectRatio(attr[i + 1], &p->alignX, &p->alignY, &p->alignType);
			}
		}
	}
}

static void nsvg__parseGradient(NSVGparser* p, const char** attr, char type)
{
	int i;
	NSVGgradientData* grad = (NSVGgradientData*)malloc(sizeof(NSVGgradientData));
	if (grad == NULL) return;
	memset(grad, 0, sizeof(NSVGgradientData));
	grad->units = NSVG_OBJECT_SPACE;
	grad->type = type;
	if (grad->type == NSVG_PAINT_LINEAR_GRADIENT) {
		grad->linear.x1 = nsvg__coord(0.0f, NSVG_UNITS_PERCENT);
		grad->linear.y1 = nsvg__coord(0.0f, NSVG_UNITS_PERCENT);
		grad->linear.x2 = nsvg__coord(100.0f, NSVG_UNITS_PERCENT);
		grad->linear.y2 = nsvg__coord(0.0f, NSVG_UNITS_PERCENT);
	} else if (grad->type == NSVG_PAINT_RADIAL_GRADIENT) {
		grad->radial.cx = nsvg__coord(50.0f, NSVG_UNITS_PERCENT);
		grad->radial.cy = nsvg__coord(50.0f, NSVG_UNITS_PERCENT);
		grad->radial.r = nsvg__coord(50.0f, NSVG_UNITS_PERCENT);
	}

	nsvg__xformIdentity(grad->xform);

	for (i = 0; attr[i]; i += 2) {
		if (strcmp(attr[i], "id") == 0) {
			strncpy(grad->id, attr[i+1], 63);
			grad->id[63] = '\0';
		} else if (!nsvg__parseAttr(p, attr[i], attr[i + 1])) {
			if (strcmp(attr[i], "gradientUnits") == 0) {
				if (strcmp(attr[i+1], "objectBoundingBox") == 0)
					grad->units = NSVG_OBJECT_SPACE;
				else
					grad->units = NSVG_USER_SPACE;
				grad->set |= NSVG_GRAD_UNITS;
			} else if (strcmp(attr[i], "gradientTransform") == 0) {
				nsvg__parseTransform(grad->xform, attr[i + 1]);
				grad->set |= NSVG_GRAD_XFORM;
			} else if (strcmp(attr[i], "cx") == 0) {
				grad->radial.cx = nsvg__parseCoordinateRaw(attr[i + 1]);
				grad->set |= NSVG_GRAD_CX;
			} else if (strcmp(attr[i], "cy") == 0) {
				grad->radial.cy = nsvg__parseCoordinateRaw(attr[i + 1]);
				grad->set |= NSVG_GRAD_CY;
			} else if (strcmp(attr[i], "r") == 0) {
				grad->radial.r = nsvg__parseCoordinateRaw(attr[i + 1]);
				grad->set |= NSVG_GRAD_R;
			} else if (strcmp(attr[i], "fx") == 0) {
				grad->radial.fx = nsvg__parseCoordinateRaw(attr[i + 1]);
				grad->set |= NSVG_GRAD_FX;
			} else if (strcmp(attr[i], "fy") == 0) {
				grad->radial.fy = nsvg__parseCoordinateRaw(attr[i + 1]);
				grad->set |= NSVG_GRAD_FY;
			} else if (strcmp(attr[i], "x1") == 0) {
				grad->linear.x1 = nsvg__parseCoordinateRaw(attr[i + 1]);
				grad->set |= NSVG_GRAD_X1;
			} else if (strcmp(attr[i], "y1") == 0) {
				grad->linear.y1 = nsvg__parseCoordinateRaw(attr[i + 1]);
				grad->set |= NSVG_GRAD_Y1;
			} else if (strcmp(attr[i], "x2") == 0) {
				grad->linear.x2 = nsvg__parseCoordinateRaw(attr[i + 1]);
				grad->set |= NSVG_GRAD_X2;
			} else if (strcmp(attr[i], "y2") == 0) {
				grad->linear.y2 = nsvg__parseCoordinateRaw(attr[i + 1]);
				grad->set |= NSVG_GRAD_Y2;
			} else if (strcmp(attr[i], "spreadMethod") == 0) {
				if (strcmp(attr[i+1], "pad") == 0)
					grad->spread = NSVG_SPREAD_PAD;
				else if (strcmp(attr[i+1], "reflect") == 0)
					grad->spread = NSVG_SPREAD_REFLECT;
				else if (strcmp(attr[i+1], "repeat") == 0)
					grad->spread = NSVG_SPREAD_REPEAT;
				grad->set |= NSVG_GRAD_SPREAD;
			} else if (strcmp(attr[i], "xlink:href") == 0 || strcmp(attr[i], "href") == 0) {
				const char *href = attr[i+1];
				strncpy(grad->ref, href+1, 62);
				grad->ref[62] = '\0';
			}
		}
	}

	grad->next = p->gradients;
	p->gradients = grad;
}

static void nsvg__parseGradientStop(NSVGparser* p, const char** attr)
{
	NSVGattrib* curAttr = nsvg__getAttr(p);
	NSVGgradientData* grad;
	NSVGgradientStop* stop;
	int i, idx;

	curAttr->stopOffset = 0;
	curAttr->stopColor = 0;
	curAttr->stopOpacity = 1.0f;

	for (i = 0; attr[i]; i += 2) {
		nsvg__parseAttr(p, attr[i], attr[i + 1]);
	}

	// Add stop to the last gradient.
	grad = p->gradients;
	if (grad == NULL) return;

	grad->nstops++;
	grad->stops = (NSVGgradientStop*)realloc(grad->stops, sizeof(NSVGgradientStop)*grad->nstops);
	if (grad->stops == NULL) return;

	// Insert
	idx = grad->nstops-1;
	for (i = 0; i < grad->nstops-1; i++) {
		if (curAttr->stopOffset < grad->stops[i].offset) {
			idx = i;
			break;
		}
	}
	if (idx != grad->nstops-1) {
		for (i = grad->nstops-1; i > idx; i--)
			grad->stops[i] = grad->stops[i-1];
	}

	stop = &grad->stops[idx];
	stop->color = curAttr->stopColor;
	stop->color |= (unsigned int)(curAttr->stopOpacity*255) << 24;
	stop->offset = curAttr->stopOffset;
}

static void nsvg__beginClip(NSVGparser* p);
static void nsvg__endClip(NSVGparser* p);

static void nsvg__startElement(void* ud, const char* el, const char** attr)
{
	NSVGparser* p = (NSVGparser*)ud;

	if (p->defsFlag) {
		// Skip everything but gradients in defs
		if (strcmp(el, "linearGradient") == 0) {
			nsvg__parseGradient(p, attr, NSVG_PAINT_LINEAR_GRADIENT);
		} else if (strcmp(el, "radialGradient") == 0) {
			nsvg__parseGradient(p, attr, NSVG_PAINT_RADIAL_GRADIENT);
		} else if (strcmp(el, "stop") == 0) {
			nsvg__parseGradientStop(p, attr);
		}
		return;
	}

	if (strcmp(el, "g") == 0) {
		nsvg__pushAttr(p);
		nsvg__parseAttribs(p, attr);
		nsvg__beginClip(p);
	} else if (strcmp(el, "path") == 0) {
		if (p->pathFlag)	// Do not allow nested paths.
			return;
		nsvg__pushAttr(p);
		nsvg__parsePath(p, attr);
		nsvg__popAttr(p);
	} else if (strcmp(el, "rect") == 0) {
		nsvg__pushAttr(p);
		nsvg__parseRect(p, attr);
		nsvg__popAttr(p);
	} else if (strcmp(el, "circle") == 0) {
		nsvg__pushAttr(p);
		nsvg__parseCircle(p, attr);
		nsvg__popAttr(p);
	} else if (strcmp(el, "ellipse") == 0) {
		nsvg__pushAttr(p);
		nsvg__parseEllipse(p, attr);
		nsvg__popAttr(p);
	} else if (strcmp(el, "line") == 0)  {
		nsvg__pushAttr(p);
		nsvg__parseLine(p, attr);
		nsvg__popAttr(p);
	} else if (strcmp(el, "polyline") == 0)  {
		nsvg__pushAttr(p);
		nsvg__parsePoly(p, attr, 0);
		nsvg__popAttr(p);
	} else if (strcmp(el, "polygon") == 0)  {
		nsvg__pushAttr(p);
		nsvg__parsePoly(p, attr, 1);
		nsvg__popAttr(p);
	} else  if (strcmp(el, "linearGradient") == 0) {
		nsvg__parseGradient(p, attr, NSVG_PAINT_LINEAR_GRADIENT);
	} else if (strcmp(el, "radialGradient") == 0) {
		nsvg__parseGradient(p, attr, NSVG_PAINT_RADIAL_GRADIENT);
	} else if (strcmp(el, "stop") == 0) {
		nsvg__parseGradientStop(p, attr);
	} else if (strcmp(el, "defs") == 0) {
		p->defsFlag = 1;
	} else if (strcmp(el, "svg") == 0) {
		nsvg__pushAttr(p);
		nsvg__parseSVG(p, attr);
		nsvg__beginClip(p);
	}
}

static void nsvg__endElement(void* ud, const char* el)
{
	NSVGparser* p = (NSVGparser*)ud;

	if (strcmp(el, "g") == 0) {
		nsvg__endClip(p);
		nsvg__popAttr(p);
	} else if (strcmp(el, "path") == 0) {
		p->pathFlag = 0;
	} else if (strcmp(el, "defs") == 0) {
		p->defsFlag = 0;
	} else if (strcmp(el, "svg") == 0) {
		nsvg__endClip(p);
		nsvg__popAttr(p);
    }
}

static void nsvg__content(void* ud, const char* s)
{
	NSVG_NOTUSED(ud);
	NSVG_NOTUSED(s);
	// empty
}

// <use> support. The XML is first read into a tree of elements, whose names & attributes point into the input, which outlives
// the parse, as a <use> can refer to an element after it, or in <defs>, which the parser skips. The tree is then replayed
// through nsvg__startElement() & nsvg__endElement(), & a <use> replays the element it refers to, under its own attributes,
// & its transform then translate(x, y). A <symbol> is only drawn by a <use>, as a group, with its viewBox fitted into the
// <use>'s width & height, but its content isn't clipped to them, as nanosvg can't clip. Symbols', clip paths', masks',
// patterns' & markers' content, which nanosvg doesn't support, is never drawn directly, so only its gradients are parsed, as in
// <defs>. A <use> of an element being drawn, i.e. one of its ancestors, is a cycle, so is skipped, & replays are capped, as
// nested <use>s can grow exponentially

#define NSVG_MAX_USE_NODES (1 << 20)	// Nodes drawn by <use>s

typedef struct NSVGnode {
	const char* name;
	const char** attr;
	const char* id;
	struct NSVGnode* parent;
	struct NSVGnode* child;
	struct NSVGnode* lastChild;
	struct NSVGnode* next;
	struct NSVGnode* hashNext;
	struct NSVGnode* allNext;	// Every node, in document order
	int active;					// Whether it's being drawn
} NSVGnode;

typedef struct NSVGdocument {
	NSVGnode root;
	NSVGnode* current;
	NSVGnode* allHead;
	NSVGnode* allTail;
	NSVGnode** buckets;			// Nodes with ids, hashed
	unsigned int nbuckets;
	int nids;
	int useNodes;				// Nodes <use>s can still draw
	int useDepth;				// How many <use>s are being drawn
	char failed;
} NSVGdocument;

static unsigned int nsvg__hashId(const char* s)
{
	unsigned int h = 2166136261u;
	while (*s)
		h = (h ^ (unsigned char)*s++) * 16777619u;
	return h;
}

static void nsvg__docStartElement(void* ud, const char* el, const char** attr)
{
	NSVGdocument* d = (NSVGdocument*)ud;
	NSVGnode* n;
	int i, count = 0;
	if (d->failed) return;
	while (attr[count]) count += 2;
	n = (NSVGnode*)calloc(1, sizeof(NSVGnode));
	if (n) n->attr = (const char**)malloc((count + 2) * sizeof(const char*));
	if (n == NULL || n->attr == NULL) {
		free(n);
		d->failed = 1;
		return;
	}
	memcpy(n->attr, attr, (count + 2) * sizeof(const char*));
	n->name = el;
	for (i = 0; i < count; i += 2)
		if (strcmp(attr[i], "id") == 0)
			n->id = attr[i + 1];
	if (n->id) d->nids++;
	n->parent = d->current;
	if (d->current->lastChild)
		d->current->lastChild->next = n;
	else
		d->current->child = n;
	d->current->lastChild = n;
	if (d->allTail) d->allTail->allNext = n; else d->allHead = n;
	d->allTail = n;
	d->current = n;
}

static void nsvg__docEndElement(void* ud, const char* el)
{
	NSVGdocument* d = (NSVGdocument*)ud;
	NSVGnode* n;
	if (d->failed) return;
	// Close the nearest open element with this name, & any left open inside it. A stray end tag is ignored
	for (n = d->current; n != &d->root; n = n->parent)
		if (strcmp(n->name, el) == 0) {
			d->current = n->parent;
			return;
		}
}

static NSVGnode* nsvg__lookupId(NSVGdocument* d, const char* id)
{
	NSVGnode* n;
	if (d->nbuckets == 0) return NULL;
	for (n = d->buckets[nsvg__hashId(id) & (d->nbuckets - 1)]; n; n = n->hashNext)
		if (strcmp(n->id, id) == 0)
			return n;
	return NULL;
}

// The node a <use> href of the form "#id" refers to, or NULL, as external references aren't supported
static NSVGnode* nsvg__findNode(NSVGdocument* d, const char* href)
{
	while (*href && nsvg__isspace(*href)) href++;
	return *href == '#' ? nsvg__lookupId(d, href + 1) : NULL;
}

// Hashes the nodes with ids. Only the first with an id is indexed, as getElementById() finds
static void nsvg__indexDocument(NSVGdocument* d)
{
	NSVGnode* n;
	unsigned int size = 16;
	while (size < 2u * (unsigned int)d->nids) size *= 2;
	d->buckets = (NSVGnode**)calloc(size, sizeof(NSVGnode*));
	if (d->buckets == NULL) return;
	d->nbuckets = size;
	for (n = d->allHead; n; n = n->allNext) {
		unsigned int b;
		if (n->id == NULL || n->id[0] == '\0' || nsvg__lookupId(d, n->id)) continue;
		b = nsvg__hashId(n->id) & (size - 1);
		n->hashNext = d->buckets[b];
		d->buckets[b] = n;
	}
}

static void nsvg__deleteDocument(NSVGdocument* d)
{
	NSVGnode *n, *next;
	for (n = d->allHead; n; n = next) {
		next = n->allNext;
		free(n->attr);
		free(n);
	}
	free(d->buckets);
}

static int nsvg__isGradientElement(const char* el)
{
	return strcmp(el, "linearGradient") == 0 || strcmp(el, "radialGradient") == 0 || strcmp(el, "stop") == 0;
}

// Whether an element's content is never drawn where it is
static int nsvg__isUndrawnElement(const char* el)
{
	return strcmp(el, "symbol") == 0 || strcmp(el, "clipPath") == 0 || strcmp(el, "mask") == 0 || strcmp(el, "pattern") == 0
		|| strcmp(el, "marker") == 0;
}

static void nsvg__drawNode(NSVGparser* p, NSVGdocument* d, NSVGnode* n);
static float nsvg__viewAlign(float content, float container, int type);

static void nsvg__drawChildren(NSVGparser* p, NSVGdocument* d, NSVGnode* n)
{
	NSVGnode* c;
	for (c = n->child; c; c = c->next)
		nsvg__drawNode(p, d, c);
}

static void nsvg__drawUse(NSVGparser* p, NSVGdocument* d, NSVGnode* use)
{
	const char* href = NULL;
	NSVGnode* ref;
	float x = 0.0f, y = 0.0f, t[6];
	float width = nsvg__actualWidth(p), height = nsvg__actualHeight(p);	// 100% by default
	int i;
	for (i = 0; use->attr[i]; i += 2)
		if (strcmp(use->attr[i], "href") == 0 || strcmp(use->attr[i], "xlink:href") == 0)
			href = use->attr[i + 1];
	ref = href ? nsvg__findNode(d, href) : NULL;
	if (ref == NULL || ref->active)
		return;

	nsvg__pushAttr(p);
	for (i = 0; use->attr[i]; i += 2) {
		const char *name = use->attr[i], *value = use->attr[i + 1];
		if (strcmp(name, "x") == 0)
			x = nsvg__parseCoordinate(p, value, nsvg__actualOrigX(p), nsvg__actualWidth(p));
		else if (strcmp(name, "y") == 0)
			y = nsvg__parseCoordinate(p, value, nsvg__actualOrigY(p), nsvg__actualHeight(p));
		else if (strcmp(name, "width") == 0)
			width = nsvg__parseCoordinate(p, value, 0.0f, nsvg__actualWidth(p));
		else if (strcmp(name, "height") == 0)
			height = nsvg__parseCoordinate(p, value, 0.0f, nsvg__actualHeight(p));
		else if (strcmp(name, "style") == 0)
			nsvg__parseStyle(p, value);
		else if (strcmp(name, "href") && strcmp(name, "xlink:href"))
			nsvg__parseAttr(p, name, value);
	}
	nsvg__beginClip(p);
	nsvg__xformSetTranslation(t, x, y);
	nsvg__xformPremultiply(nsvg__getAttr(p)->xform, t);

	if (strcmp(ref->name, "symbol") == 0) {
		// A viewBox is fitted into the <use>'s width & height by its preserveAspectRatio, xMidYMid meet by default
		float vb[4] = { 0, 0, 0, 0 };
		int alignX = NSVG_ALIGN_MID, alignY = NSVG_ALIGN_MID, alignType = NSVG_ALIGN_MEET;
		for (i = 0; ref->attr[i]; i += 2)
			if (strcmp(ref->attr[i], "viewBox") == 0)
				sscanf(ref->attr[i + 1], "%f%*[%%, \t]%f%*[%%, \t]%f%*[%%, \t]%f", &vb[0], &vb[1], &vb[2], &vb[3]);
			else if (strcmp(ref->attr[i], "preserveAspectRatio") == 0)
				nsvg__parseAspectRatio(ref->attr[i + 1], &alignX, &alignY, &alignType);
		if (vb[2] > 0.0f && vb[3] > 0.0f && width > 0.0f && height > 0.0f) {
			float sx = width / vb[2], sy = height / vb[3];
			if (alignType != NSVG_ALIGN_NONE)
				sx = sy = alignType == NSVG_ALIGN_SLICE ? nsvg__maxf(sx, sy) : nsvg__minf(sx, sy);
			t[0] = sx, t[1] = 0, t[2] = 0, t[3] = sy;
			t[4] = (alignType == NSVG_ALIGN_NONE ? 0 : nsvg__viewAlign(vb[2] * sx, width, alignX)) - vb[0] * sx;
			t[5] = (alignType == NSVG_ALIGN_NONE ? 0 : nsvg__viewAlign(vb[3] * sy, height, alignY)) - vb[1] * sy;
			nsvg__xformPremultiply(nsvg__getAttr(p)->xform, t);
		}
		ref->active = 1, d->useDepth++;
		nsvg__pushAttr(p);
		nsvg__parseAttribs(p, ref->attr);
		nsvg__drawChildren(p, d, ref);
		nsvg__popAttr(p);
		ref->active = 0, d->useDepth--;
	} else {
		d->useDepth++;
		nsvg__drawNode(p, d, ref);
		d->useDepth--;
	}
	nsvg__endClip(p);
	nsvg__popAttr(p);
}

// Parses every gradient, & its stops, before the replay, as shapes can refer to gradients after them, e.g. in a <defs> at the
// end. Each is parsed in its own attributes, as the parser's are its ancestors', which it mustn't change
static void nsvg__parseGradients(NSVGparser* p, NSVGnode* n)
{
	NSVGnode* c;
	for (c = n->child; c; c = c->next) {
		if (strcmp(c->name, "linearGradient") == 0 || strcmp(c->name, "radialGradient") == 0) {
			NSVGnode* stop;
			nsvg__pushAttr(p);
			nsvg__parseGradient(p, c->attr, c->name[0] == 'l' ? NSVG_PAINT_LINEAR_GRADIENT : NSVG_PAINT_RADIAL_GRADIENT);
			for (stop = c->child; stop; stop = stop->next)
				if (strcmp(stop->name, "stop") == 0)
					nsvg__parseGradientStop(p, stop->attr);
			nsvg__popAttr(p);
		} else
			nsvg__parseGradients(p, c);
	}
}

// Replays an element & its children. In <defs>, only gradients are parsed, & other elements are skipped, so their end tags
// don't pop attributes they didn't push. Gradients are parsed before the replay, so are skipped
static void nsvg__drawNode(NSVGparser* p, NSVGdocument* d, NSVGnode* n)
{
	int skip;
	if (n->active || (d->useDepth && d->useNodes-- <= 0))
		return;
	if (strcmp(n->name, "linearGradient") == 0 || strcmp(n->name, "radialGradient") == 0)
		return;
	if (nsvg__isUndrawnElement(n->name)) {	// Only its gradients are parsed, as in <defs>
		char defsFlag = p->defsFlag;
		p->defsFlag = 1;
		nsvg__drawChildren(p, d, n);
		p->defsFlag = defsFlag;
		return;
	}
	if (strcmp(n->name, "use") == 0) {
		if (!p->defsFlag)
			nsvg__drawUse(p, d, n);
		return;
	}
	skip = p->defsFlag && !nsvg__isGradientElement(n->name);
	if (!skip)
		nsvg__startElement(p, n->name, n->attr);
	n->active = 1;
	nsvg__drawChildren(p, d, n);
	n->active = 0;
	if (!skip)
		nsvg__endElement(p, n->name);
}

// clip-path support. A clip path is parsed where it's referred to, in the referring element's user space, or for
// clipPathUnits="objectBoundingBox", the unit square of its bounds, & its <clipPath>'s transform, as an NSVGclipPath of
// its content's shapes. References with the same <clipPath> & transform share one. A group's objectBoundingBox clip path is
// resolved at its end, by its shapes' bounds. A clip-path that doesn't refer to a <clipPath> is ignored, as browsers do,
// & clip-paths in a clip path's content, & of a <clipPath> itself, aren't supported

typedef struct NSVGclipCache {
	NSVGnode* node;
	float xform[6];
	NSVGclipPath* clip;
	struct NSVGclipCache* next;
} NSVGclipCache;

static void nsvg__deleteClipCache(NSVGclipCache* c)
{
	while (c) {
		NSVGclipCache* next = c->next;
		free(c);
		c = next;
	}
}

static void nsvg__addClip(NSVGattrib* attr, NSVGclipPath* clip)
{
	if (clip && attr->nclips < NSVG_MAX_CLIPS)
		attr->clips[attr->nclips++] = clip;
}

// The <clipPath> with an id, or NULL
static NSVGnode* nsvg__findClipPath(NSVGparser* p, const char* id)
{
	NSVGnode* n = p->doc ? nsvg__lookupId(p->doc, id) : NULL;
	return n && strcmp(n->name, "clipPath") == 0 ? n : NULL;
}

static int nsvg__isObjectBoundingBoxClip(NSVGnode* n)
{
	int i;
	for (i = 0; n->attr[i]; i += 2)
		if (strcmp(n->attr[i], "clipPathUnits") == 0)
			return strcmp(n->attr[i + 1], "objectBoundingBox") == 0;
	return 0;
}

// The clip path a <clipPath> id makes for an element with a transform, & for objectBoundingBox units, bounds in its user
// space, or NULL if the id isn't a <clipPath>, or its bounds are empty
static NSVGclipPath* nsvg__clipInstance(NSVGparser* p, const char* id, const float* xform, const float* bounds)
{
	NSVGnode* n = nsvg__findClipPath(p, id);
	NSVGclipCache* c;
	NSVGclipPath* clip;
	NSVGattrib* attr;
	NSVGshape *shapes, *tail;
	float t[6];
	char defsFlag;
	if (n == NULL || n->active)
		return NULL;
	memcpy(t, xform, sizeof(t));
	if (nsvg__isObjectBoundingBoxClip(n)) {
		float b[6];
		if (bounds == NULL || bounds[2] <= bounds[0] || bounds[3] <= bounds[1])
			return NULL;
		b[0] = bounds[2] - bounds[0], b[1] = 0, b[2] = 0, b[3] = bounds[3] - bounds[1], b[4] = bounds[0], b[5] = bounds[1];
		nsvg__xformPremultiply(t, b);
	}
	for (c = p->clipCache; c; c = c->next)
		if (c->node == n && memcmp(c->xform, t, sizeof(t)) == 0)
			return c->clip;

	clip = (NSVGclipPath*)calloc(1, sizeof(NSVGclipPath));
	c = (NSVGclipCache*)calloc(1, sizeof(NSVGclipCache));
	if (clip == NULL || c == NULL) {
		free(clip), free(c);
		return NULL;
	}
	strncpy(clip->id, id, 63);

	// Its content's shapes, parsed into an empty list, in its own attributes, which aren't inherited from the referrer
	shapes = p->image->shapes, tail = p->shapesTail, defsFlag = p->defsFlag;
	p->image->shapes = NULL, p->shapesTail = NULL, p->defsFlag = 0, p->clipFlag = 1;
	nsvg__pushAttr(p);
	attr = nsvg__getAttr(p);
	memcpy(attr->xform, t, sizeof(t));
	attr->clipRule = NSVG_FILLRULE_NONZERO, attr->visible = 1, attr->nclips = 0;
	nsvg__parseAttribs(p, n->attr);
	n->active = 1;
	nsvg__drawChildren(p, p->doc, n);
	n->active = 0;
	nsvg__popAttr(p);
	clip->shapes = p->image->shapes;
	p->image->shapes = shapes, p->shapesTail = tail, p->defsFlag = defsFlag, p->clipFlag = 0;

	clip->next = p->image->clipPaths;
	p->image->clipPaths = clip;
	c->node = n, memcpy(c->xform, t, sizeof(t)), c->clip = clip;
	c->next = p->clipCache;
	p->clipCache = c;
	return clip;
}

// At an element that can contain shapes, after its attributes: its clip-path, unless it has objectBoundingBox units, which
// needs its shapes' bounds, so is resolved at its end
static void nsvg__beginClip(NSVGparser* p)
{
	NSVGattrib* attr = nsvg__getAttr(p);
	NSVGnode* n;
	if (p->clipFlag || attr->clipPathId[0] == '\0')
		return;
	if ((n = nsvg__findClipPath(p, attr->clipPathId)) && nsvg__isObjectBoundingBoxClip(n)) {
		attr->deferClip = 1, attr->clipShapes = p->shapesTail;
		return;
	}
	nsvg__addClip(attr, nsvg__clipInstance(p, attr->clipPathId, attr->xform, NULL));
	attr->clipPathId[0] = '\0';
}

// At the element's end: its deferred clip path, by the bounds of its shapes in its user space, added to theirs
static void nsvg__endClip(NSVGparser* p)
{
	NSVGattrib* attr = nsvg__getAttr(p);
	NSVGshape *first, *shape;
	NSVGclipPath* clip;
	float inv[6], bounds[4], b[4];
	if (!attr->deferClip)
		return;
	attr->deferClip = 0;
	first = attr->clipShapes ? attr->clipShapes->next : p->image->shapes;
	if (first == NULL)
		return;
	nsvg__xformInverse(inv, attr->xform);
	for (shape = first; shape; shape = shape->next) {
		nsvg__getLocalBounds(b, shape, inv);
		if (shape == first)
			memcpy(bounds, b, sizeof(b));
		else
			bounds[0] = nsvg__minf(bounds[0], b[0]), bounds[1] = nsvg__minf(bounds[1], b[1]),
			bounds[2] = nsvg__maxf(bounds[2], b[2]), bounds[3] = nsvg__maxf(bounds[3], b[3]);
	}
	if ((clip = nsvg__clipInstance(p, attr->clipPathId, attr->xform, bounds)) == NULL)
		return;
	for (shape = first; shape; shape = shape->next) {
		NSVGclipPath** clips;
		if (shape->clipPathCount >= NSVG_MAX_CLIPS)
			continue;
		clips = (NSVGclipPath**)realloc(shape->clipPaths, (shape->clipPathCount + 1) * sizeof(NSVGclipPath*));
		if (clips == NULL)
			continue;
		shape->clipPaths = clips;
		shape->clipPaths[shape->clipPathCount++] = clip;
	}
}

static void nsvg__imageBounds(NSVGparser* p, float* bounds)
{
	NSVGshape* shape;
	shape = p->image->shapes;
	if (shape == NULL) {
		bounds[0] = bounds[1] = bounds[2] = bounds[3] = 0.0;
		return;
	}
	bounds[0] = shape->bounds[0];
	bounds[1] = shape->bounds[1];
	bounds[2] = shape->bounds[2];
	bounds[3] = shape->bounds[3];
	for (shape = shape->next; shape != NULL; shape = shape->next) {
		bounds[0] = nsvg__minf(bounds[0], shape->bounds[0]);
		bounds[1] = nsvg__minf(bounds[1], shape->bounds[1]);
		bounds[2] = nsvg__maxf(bounds[2], shape->bounds[2]);
		bounds[3] = nsvg__maxf(bounds[3], shape->bounds[3]);
	}
}

static float nsvg__viewAlign(float content, float container, int type)
{
	if (type == NSVG_ALIGN_MIN)
		return 0;
	else if (type == NSVG_ALIGN_MAX)
		return container - content;
	// mid
	return (container - content) * 0.5f;
}

static void nsvg__scaleGradient(NSVGgradient* grad, float tx, float ty, float sx, float sy)
{
	float t[6];
	nsvg__xformSetTranslation(t, tx, ty);
	nsvg__xformMultiply (grad->xform, t);

	nsvg__xformSetScale(t, sx, sy);
	nsvg__xformMultiply (grad->xform, t);
}

static void nsvg__scaleToViewbox(NSVGparser* p, const char* units)
{
	NSVGshape* shape;
	NSVGclipPath* clip;
	NSVGpath* path;
	float tx, ty, sx, sy, us, bounds[4], t[6], avgs;
	int i;
	float* pt;

	// Guess image size if not set completely.
	nsvg__imageBounds(p, bounds);

	if (p->viewWidth == 0) {
		if (p->image->width > 0) {
			p->viewWidth = p->image->width;
		} else {
			p->viewMinx = bounds[0];
			p->viewWidth = bounds[2] - bounds[0];
		}
	}
	if (p->viewHeight == 0) {
		if (p->image->height > 0) {
			p->viewHeight = p->image->height;
		} else {
			p->viewMiny = bounds[1];
			p->viewHeight = bounds[3] - bounds[1];
		}
	}
	if (p->image->width == 0)
		p->image->width = p->viewWidth;
	if (p->image->height == 0)
		p->image->height = p->viewHeight;

	tx = -p->viewMinx;
	ty = -p->viewMiny;
	sx = p->viewWidth > 0 ? p->image->width / p->viewWidth : 0;
	sy = p->viewHeight > 0 ? p->image->height / p->viewHeight : 0;
	// Unit scaling
	us = 1.0f / nsvg__convertToPixels(p, nsvg__coord(1.0f, nsvg__parseUnits(units)), 0.0f, 1.0f);

	// Fix aspect ratio
	if (p->alignType == NSVG_ALIGN_MEET) {
		// fit whole image into viewbox
		sx = sy = nsvg__minf(sx, sy);
		tx += nsvg__viewAlign(p->viewWidth*sx, p->image->width, p->alignX) / sx;
		ty += nsvg__viewAlign(p->viewHeight*sy, p->image->height, p->alignY) / sy;
	} else if (p->alignType == NSVG_ALIGN_SLICE) {
		// fill whole viewbox with image
		sx = sy = nsvg__maxf(sx, sy);
		tx += nsvg__viewAlign(p->viewWidth*sx, p->image->width, p->alignX) / sx;
		ty += nsvg__viewAlign(p->viewHeight*sy, p->image->height, p->alignY) / sy;
	}

	// Transform
	sx *= us;
	sy *= us;
	avgs = (sx+sy) / 2.0f;
	for (clip = p->image->clipPaths; clip != NULL; clip = clip->next)
		for (shape = clip->shapes; shape != NULL; shape = shape->next) {
			for (i = 0; i < 4; i++)
				shape->bounds[i] = (shape->bounds[i] + (i & 1 ? ty : tx)) * (i & 1 ? sy : sx);
			for (path = shape->paths; path != NULL; path = path->next) {
				for (i = 0; i < 4; i++)
					path->bounds[i] = (path->bounds[i] + (i & 1 ? ty : tx)) * (i & 1 ? sy : sx);
				for (i = 0; i < path->npts; i++) {
					pt = &path->pts[i*2];
					pt[0] = (pt[0] + tx) * sx;
					pt[1] = (pt[1] + ty) * sy;
				}
			}
		}
	for (shape = p->image->shapes; shape != NULL; shape = shape->next) {
		shape->bounds[0] = (shape->bounds[0] + tx) * sx;
		shape->bounds[1] = (shape->bounds[1] + ty) * sy;
		shape->bounds[2] = (shape->bounds[2] + tx) * sx;
		shape->bounds[3] = (shape->bounds[3] + ty) * sy;
		for (path = shape->paths; path != NULL; path = path->next) {
			path->bounds[0] = (path->bounds[0] + tx) * sx;
			path->bounds[1] = (path->bounds[1] + ty) * sy;
			path->bounds[2] = (path->bounds[2] + tx) * sx;
			path->bounds[3] = (path->bounds[3] + ty) * sy;
			for (i =0; i < path->npts; i++) {
				pt = &path->pts[i*2];
				pt[0] = (pt[0] + tx) * sx;
				pt[1] = (pt[1] + ty) * sy;
			}
		}

        /**/
		if (shape->fill.type == NSVG_PAINT_LINEAR_GRADIENT || shape->fill.type == NSVG_PAINT_RADIAL_GRADIENT) {
			nsvg__scaleGradient(shape->fill.gradient, tx,ty, sx,sy);
			memcpy(t, shape->fill.gradient->xform, sizeof(float)*6);
			nsvg__xformInverse(shape->fill.gradient->xform, t);
		}
		if (shape->stroke.type == NSVG_PAINT_LINEAR_GRADIENT || shape->stroke.type == NSVG_PAINT_RADIAL_GRADIENT) {
			nsvg__scaleGradient(shape->stroke.gradient, tx,ty, sx,sy);
			memcpy(t, shape->stroke.gradient->xform, sizeof(float)*6);
			nsvg__xformInverse(shape->stroke.gradient->xform, t);
		}
         
		shape->strokeWidth *= avgs;
		shape->strokeDashOffset *= avgs;
		for (i = 0; i < shape->strokeDashCount; i++)
			shape->strokeDashArray[i] *= avgs;
	}
}

NSVGimage* nsvgParse(char* input, const char* units, float dpi)
{
	NSVGparser* p;
	NSVGimage* ret = 0;

	p = nsvg__createParser();
	if (p == NULL) {
		return NULL;
	}
	p->dpi = dpi;

	{
		NSVGdocument d;
		memset(&d, 0, sizeof(d));
		d.root.name = "";
		d.current = &d.root;
		d.useNodes = NSVG_MAX_USE_NODES;
		nsvg__parseXML(input, nsvg__docStartElement, nsvg__docEndElement, nsvg__content, &d);
		nsvg__indexDocument(&d);
		p->doc = &d;
		nsvg__parseGradients(p, &d.root);
		nsvg__drawChildren(p, &d, &d.root);
		p->doc = NULL;
		nsvg__deleteDocument(&d);
	}

	// Scale to viewBox
    if (units)
        nsvg__scaleToViewbox(p, units);

	ret = p->image;
	p->image = NULL;

	nsvg__deleteParser(p);

	return ret;
}

NSVGimage* nsvgParseFromFile(const char* filename, const char* units, float dpi)
{
	FILE* fp = NULL;
	size_t size;
	char* data = NULL;
	NSVGimage* image = NULL;

	fp = fopen(filename, "rb");
	if (!fp) goto error;
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	data = (char*)malloc(size+1);
	if (data == NULL) goto error;
	if (fread(data, 1, size, fp) != size) goto error;
	data[size] = '\0';	// Must be null terminated.
	fclose(fp);
	image = nsvgParse(data, units, dpi);
	free(data);

	return image;

error:
	if (fp) fclose(fp);
	if (data) free(data);
	if (image) nsvgDelete(image);
	return NULL;
}

NSVGpath* nsvgDuplicatePath(NSVGpath* p)
{
    NSVGpath* res = NULL;

    if (p == NULL)
        return NULL;

    res = (NSVGpath*)malloc(sizeof(NSVGpath));
    if (res == NULL) goto error;
    memset(res, 0, sizeof(NSVGpath));

    res->pts = (float*)malloc(p->npts*2*sizeof(float));
    if (res->pts == NULL) goto error;
    memcpy(res->pts, p->pts, p->npts * sizeof(float) * 2);
    res->npts = p->npts;

    memcpy(res->bounds, p->bounds, sizeof(p->bounds));

    res->closed = p->closed;

    return res;

error:
    if (res != NULL) {
        free(res->pts);
        free(res);
    }
    return NULL;
}

static void nsvg__deleteShapes(NSVGshape* shape)
{
	NSVGshape *snext;
	while (shape != NULL) {
		snext = shape->next;
		nsvg__deletePaths(shape->paths);
		nsvg__deletePaint(&shape->fill);
		nsvg__deletePaint(&shape->stroke);
		free(shape->clipPaths);
		free(shape);
		shape = snext;
	}
}

void nsvgDelete(NSVGimage* image)
{
	NSVGclipPath *clip, *next;
	if (image == NULL) return;
	nsvg__deleteShapes(image->shapes);
	for (clip = image->clipPaths; clip != NULL; clip = next) {
		next = clip->next;
		nsvg__deleteShapes(clip->shapes);
		free(clip);
	}
	free(image);
}

#endif
