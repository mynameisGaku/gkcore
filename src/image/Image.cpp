#include "Image.h"

namespace gk::detail {
ImageResource* CreateImageResource() {
    ImageResource* image = nullptr;
    try { image = new ImageResource; } catch (...) { return nullptr; }
    image->reference.references = 1;
    image->reference.destroy = DestroyImageResource;
    image->width = 0;
    image->height = 0;
    return image;
}

void DestroyImageResource(RefCounted* object) {
    delete reinterpret_cast<ImageResource*>(object);
}
}
