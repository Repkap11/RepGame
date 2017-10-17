typedef struct {
    int top;
    int side;
    int bottom;
    float alpha;
    float height;
} BlockTextureMap;

typedef struct {
    int id;
    BlockTextureMap btm;
    int opacity;
} Block;

enum BlockTypes { air, grass, water };