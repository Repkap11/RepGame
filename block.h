typedef struct {
    int top;
    int side;
    int bottom;
} BlockTextureMap;

typedef struct {
    int id;
    BlockTextureMap btm;
    int transparent;
    int opacity;
} Block;

enum BlockTypes { air, grass, water };