/*
 * scene.h - the 3-D scene the software pass draws, frame by frame, for the GPU's view (M7).
 *
 * scene.c collects it from the hooks in bak_port.h: one frame per pass of the renderer (from
 * one r3d_camera_setup_view to the next), in the order the software draws it. The sky and the
 * ground's bands come first, as screen-space operations; then the items: faces (their corners
 * in camera space), billboards, circles and lines on screen, each with what the software did
 * with it and the clip rectangle it was drawn within. A frame
 * is finished when the next one begins or when the page it was drawn into is flipped to the
 * front (scene_page_flipped); the last finished frame of each VGA page is kept. The images the
 * frames name are copied into textures of their own as they first appear, so a reader never
 * touches the game's memory.
 */
#ifndef BAK_SCENE_H
#define BAK_SCENE_H

#include <stdbool.h>
#include <stdint.h>

/* Camera space in world units: x to the right, y ahead (the depth), z up. */
typedef struct SceneVertex {
    float x, y, z;
} SceneVertex;

/* An image as palette indices, row by row (0 is transparent, but for the sky's panorama). */
typedef struct SceneTexture {
    uint32_t id; /* from 1 */
    int16_t width, height;
    uint8_t *indices;
} SceneTexture;

/* The ground's strip texture as vgablit.c's tiled spans walk it: the screen row y reads the
 * texture's bytes from siBase + (y - rowSub) * stride when rowCount is 0, else from siBase,
 * stepping by stride and back to siBase every rowCount rows; byte b holds pixels 4 * (b % 80)
 * + 0..3 of row b / 80 of the 320-wide picture at AC00h, and screen pixel x reads the byte x / 4
 * along, its plane x % 4. */
typedef struct SceneStrip {
    uint16_t siBase;
    int16_t stride;
    uint16_t rowSub, rowCount;
} SceneStrip;

typedef enum SceneItemType {
    SCENE_FACE,
    SCENE_BILLBOARD,
    SCENE_CIRCLE,
    SCENE_LINE
} SceneItemType;

/* What the software did with an item. */
typedef enum SceneFate {
    SCENE_DRAWN,   /* drawn */
    SCENE_CLIPPED, /* nothing left of it in front of the view plane or inside the view */
    SCENE_DROPPED, /* a textured quad the near clip left with other than four corners */
    SCENE_FOGGED,  /* a textured quad beyond the fog's far end, or without an image */
} SceneFate;

typedef struct SceneItem {
    uint8_t type;     /* SceneItemType */
    uint8_t fate;     /* SceneFate */
    uint8_t corners;  /* a face's corners, from firstVertex on */
    uint8_t lod;      /* a face's producer's level of detail */
    uint8_t flags;    /* a face record's flags */
    uint8_t whole;    /* a face that reached its sink unclipped */
    uint8_t filled;   /* a flat face: filled, or its outline only */
    uint8_t fill;     /* a flat face's, a circle's or a line's colour */
    uint8_t outline;  /* a flat face's outline colour */
    uint8_t textured; /* a face drawn with an image (texture) */
    uint8_t striped;  /* a flat face filled from the ground's strip */
    uint8_t vertical; /* a textured face the software drew by its vertical-sided path */
    uint8_t flip;     /* a billboard's flip bits: 1 vertical, 2 horizontal */
    int8_t remap;     /* the remap (fog) table's row in g_polyrastTables, or -1 */
    /* A textured face: the image corner each corner takes (0 top left, 1 top right, 2 bottom
     * right, 3 bottom left): the software's rule for a quad it draws whole, applied to the
     * face's corners, so that a view or a clip does not change it. */
    uint8_t imageCorner[4];
    uint16_t object;      /* the item's object, in the frame's objects */
    uint32_t firstVertex; /* a face's corners, a billboard's anchor, a circle's centre */
    uint32_t texture;     /* a textured face's or a billboard's image, 0 for none */
    SceneStrip strip;     /* a striped face's */
    /* A face's polygon as its sink took it: left, top, right, bottom; a billboard's sprite on
     * screen: x, y, w, h; a circle's centre and radius: x, y, r; a line's ends: x0, y0, x1, y1. */
    int16_t rect[4];
    /* The clip rectangle it was drawn within: left, top, right, bottom (a sprite cut at the
     * ground it sinks into is narrower than the view); none, the whole of int16_t's range. */
    int16_t clip[4];
    const void *id;       /* a face's record */
} SceneItem;

#define SCENE_NO_OBJECT 0xffff

typedef struct SceneObject {
    SceneVertex origin; /* camera space, world units */
    float extent;       /* the bounding half extent */
    uint8_t priority;   /* the shape's draw priority: nonzero ones are drawn first */
    const void *shape;
} SceneObject;

typedef enum SceneSkyKind {
    SCENE_SKY_BAND,   /* rows y .. y + h - 1, pixels 13-306, in colour */
    SCENE_SKY_SHADED, /* rows y .. y + h - 1, pixels 13-306, shaded from the bottom up */
    SCENE_SKY_SPRITE, /* texture at x, y, opaque */
    SCENE_SKY_RECT,   /* x, y, w, h in colour; with a dither colour, pixel (px, py) is colour
                         where px + py + x is even, dither where it is odd */
    SCENE_SKY_GROUND, /* x, y, w, h from the ground's strip texture */
} SceneSkyKind;

typedef struct SceneSkyOp {
    uint8_t kind; /* SceneSkyKind */
    uint8_t colour;
    uint8_t dither; /* a rectangle's second colour, 0 for none */
    int16_t x, y, w, h;
    uint32_t texture;
    SceneStrip strip;
} SceneSkyOp;

#define SCENE_SKY_MAX 16

typedef struct SceneFrame {
    uint32_t serial; /* frames counted from 1 */
    uint16_t page;   /* the VGA page drawn into (its segment); 0 until something is drawn */
    bool mixedPages; /* something was drawn into another page as well */
    bool background; /* drawn into page 1, which is never shown itself (picking's readback;
                        combat's background, which its frames are copied over) */
    uint8_t tag;     /* the tag its draws' pixels carry in display memory (bak_video.h): 1-255
                        for a view the window shows (bak_scene_view), else 0; no two frames kept
                        have the same */
    /* the camera */
    int32_t camPos[3];
    int16_t camAngles[3];
    int16_t clip[4]; /* the viewport: left, top, right, bottom */
    int16_t centerX, centerY;
    uint8_t screenShift; /* the focal length is 2^screenShift pixels */
    /* the sky */
    bool hasHorizon;
    int16_t horizon[4]; /* x0, y0, x1, y1 */
    uint8_t horizonReject;
    uint16_t heading;
    SceneSkyOp sky[SCENE_SKY_MAX];
    uint32_t skyCount;
    /* The ground's strip texture as the frame's strips read it: the scratch page at AC00h when
     * the first was drawn (combat keeps its background there later), 65536 bytes */
    bool hasGround;
    uint8_t *ground;
    /* the items, in the order drawn */
    SceneItem *items;
    uint32_t itemCount, itemCap;
    SceneVertex *vertices;
    uint32_t vertexCount, vertexCap;
    SceneObject *objects;
    uint32_t objectCount, objectCap;
} SceneFrame;

/* Collect frames (the GPU's view wants them); BAK_SCENE_CHECK collects them too. */
void scene_enable(bool on);
/* The frame being drawn ends here and becomes its page's last finished frame, if anything was
 * drawn; what comes after it starts a frame with the same camera. */
void scene_finish(void);
/* A page became the front one (vga_vsync_wait): a frame drawn into it is finished. */
void scene_page_flipped(uint16_t page);
/* The last finished frame drawn into the page, or NULL. It stays as it is until the next frame
 * of that page finishes. */
const SceneFrame *scene_latest(uint16_t page);
/* The finished frame whose pixels carry the tag, or NULL. */
const SceneFrame *scene_frame_tagged(uint8_t tag);
/*
 * The frames a picture shows, given how many of its pixels carry each tag: the newest frame its
 * tags name, and while the one taken last covers none of its view itself (no sky or background:
 * combat's actors, drawn over the background copied under them), the frame under it, if it
 * shares the view. Oldest first, as they are painted, ending in NULL; their count.
 */
#define SCENE_SHOWN_MAX 4
unsigned scene_frames_shown(const unsigned counts[256], const SceneFrame *out[SCENE_SHOWN_MAX + 1]);
/* A texture by its id, or NULL. Textures stay. */
const SceneTexture *scene_texture(uint32_t id);
/* Forget every frame and texture. */
void scene_reset(void);
/* Called with each frame as it finishes (NULL: nobody). */
void scene_set_finished_hook(void (*hook)(const SceneFrame *frame));
/* The remap tables the blits read (POLYRAST's g_polyrastTables: 15 of 256 entries). */
const uint8_t *scene_remap_tables(void);

/* The sky's shade ramp, from its bottom row up (vgafill.c, detail level 0's sky). */
extern const unsigned char g_skyShadeRamp[34];

#endif
