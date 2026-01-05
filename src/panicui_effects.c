#include "include/panicui.h"
#include "include/graphics/graphics_manager.h"
#include "include/system.h"
#include "include/math.h"
#include "include/libc/stdlib.h"

#ifndef ARCH_64BIT
#if defined(__x86_64__) || defined(_M_X64)
#define ARCH_64BIT 1
#else
#define ARCH_64BIT 0
#endif
#endif

// Define RAND_MAX if not defined
#ifndef RAND_MAX
#define RAND_MAX 32767
#endif

#if ARCH_64BIT
/* On 64-bit builds we disable heavy trig-based effects to avoid SSE usage */
void panicui_draw_glow_effect(graphics_rect_t bounds, graphics_color_t color, uint32_t radius) {
    (void)bounds; (void)color; (void)radius;
}

void panicui_init_effects(void) {}
void panicui_add_sparkle_effect(int32_t x, int32_t y) { (void)x; (void)y; }
void panicui_draw_particle_system(void) {}
void panicui_draw_scanlines(void) {}
void panicui_draw_vignette(void) {}
void panicui_draw_animated_background(void) {}
void panicui_update_particles(void) {}
void panicui_draw_particles(void) {}
void panicui_draw_sparkles(void) {}

#else

// =============================================================================
// ENHANCED VISUAL EFFECTS
// =============================================================================

#define PARTICLE_COUNT 50
#define MAX_SPARKLES 20

typedef struct {
    float x, y;
    float vx, vy;
    float life;
    graphics_color_t color;
    uint8_t size;
} particle_t;

typedef struct {
    float x, y;
    float life;
    float alpha;
    uint32_t creation_time;
} sparkle_t;

static particle_t g_particles[PARTICLE_COUNT];
static sparkle_t g_sparkles[MAX_SPARKLES];
static bool g_effects_initialized = false;
static uint32_t g_frame_time = 0;

void panicui_init_effects(void) {
    if (g_effects_initialized) return;
    
    // Initialize floating particles
    for (int i = 0; i < PARTICLE_COUNT; i++) {
        g_particles[i].x = (float)(rand() % 1024);
        g_particles[i].y = (float)(rand() % 768);
        g_particles[i].vx = ((float)rand() / RAND_MAX - 0.5f) * 0.5f;
        g_particles[i].vy = ((float)rand() / RAND_MAX - 0.5f) * 0.5f;
        g_particles[i].life = 1.0f;
        g_particles[i].color = (graphics_color_t){
            100 + rand() % 155,
            100 + rand() % 155, 
            150 + rand() % 105,
            80 + rand() % 100
        };
        g_particles[i].size = 1 + rand() % 3;
    }
    
    // Initialize sparkles
    for (int i = 0; i < MAX_SPARKLES; i++) {
        g_sparkles[i].life = 0.0f;
        g_sparkles[i].alpha = 0.0f;
        g_sparkles[i].creation_time = 0;
    }
    
    g_effects_initialized = true;
}

void panicui_draw_gradient_rect(graphics_rect_t bounds, graphics_color_t start, graphics_color_t end, bool vertical) {
    if (vertical) {
        // Vertical gradient
        for (uint32_t y = 0; y < bounds.height; y++) {
            float t = (float)y / bounds.height;
            graphics_color_t color = {
                start.r + (uint8_t)((end.r - start.r) * t),
                start.g + (uint8_t)((end.g - start.g) * t),
                start.b + (uint8_t)((end.b - start.b) * t),
                start.a + (uint8_t)((end.a - start.a) * t)
            };
            
            graphics_rect_t line = {bounds.x, bounds.y + y, bounds.width, 1};
            graphics_draw_rect(&line, color, true);
        }
    } else {
        // Horizontal gradient
        for (uint32_t x = 0; x < bounds.width; x++) {
            float t = (float)x / bounds.width;
            graphics_color_t color = {
                start.r + (uint8_t)((end.r - start.r) * t),
                start.g + (uint8_t)((end.g - start.g) * t),
                start.b + (uint8_t)((end.b - start.b) * t),
                start.a + (uint8_t)((end.a - start.a) * t)
            };
            
            graphics_rect_t line = {bounds.x + x, bounds.y, 1, bounds.height};
            graphics_draw_rect(&line, color, true);
        }
    }
}

void panicui_draw_glow_effect(graphics_rect_t bounds, graphics_color_t color, uint32_t radius) {
    // Draw multiple layers of decreasing alpha for glow effect
    for (uint32_t r = radius; r > 0; r--) {
        graphics_color_t glow_color = color;
        glow_color.a = (color.a * r) / (radius + 1);
        
        // Draw expanded rectangle for each glow layer
        graphics_rect_t glow_rect = {
            bounds.x - r,
            bounds.y - r,
            bounds.width + 2 * r,
            bounds.height + 2 * r
        };
        
        // Draw glow border only (not filled)
        graphics_draw_rect(&glow_rect, glow_color, false);
        
        // Add corner pixels for rounded glow
        for (int angle = 0; angle < 360; angle += 45) {
            int x_offset = (int)(r * cos(angle * M_PI / 180.0));
            int y_offset = (int)(r * sin(angle * M_PI / 180.0));
            
            graphics_draw_pixel(bounds.x + bounds.width/2 + x_offset, 
                               bounds.y + bounds.height/2 + y_offset, 
                               glow_color);
        }
    }
}

void panicui_draw_animated_background(void) {
    panicui_context_t* ctx = panicui_get_context();
    if (!ctx) return;
    
    panicui_init_effects();
    
    // Create animated gradient background
    graphics_color_t bg_start = PANICUI_COLOR_BG_PRIMARY;
    graphics_color_t bg_end = PANICUI_COLOR_BG_SECONDARY;
    
    // Animate the background colors slightly
    float time = (g_frame_time % 3000) / 3000.0f;
    float wave = sin(time * 2 * M_PI) * 0.1f + 0.9f;
    
    bg_end.r = (uint8_t)(bg_end.r * wave);
    bg_end.g = (uint8_t)(bg_end.g * wave);
    bg_end.b = (uint8_t)(bg_end.b * wave);
    
    graphics_rect_t screen_rect = {0, 0, ctx->screen_width, ctx->screen_height};
    panicui_draw_gradient_rect(screen_rect, bg_start, bg_end, true);
    
    // Add subtle geometric patterns
    graphics_color_t pattern_color = {40, 40, 60, 30};
    
    // Draw diagonal lines pattern
    for (int y = 0; y < (int)ctx->screen_height; y += 60) {
        for (int x = -100; x < (int)ctx->screen_width + 100; x += 60) {
            int line_x = x + (int)(sin(time * 2 * M_PI + y * 0.01f) * 20);
            graphics_draw_line(line_x, y, line_x + 30, y + 30, pattern_color);
        }
    }
    
    g_frame_time++;
}

void panicui_draw_particle_system(void) {
    panicui_context_t* ctx = panicui_get_context();
    if (!ctx) return;
    
    panicui_init_effects();
    
    // Update and draw particles
    for (int i = 0; i < PARTICLE_COUNT; i++) {
        particle_t* p = &g_particles[i];
        
        // Update position
        p->x += p->vx;
        p->y += p->vy;
        
        // Add some gravity and air resistance
        p->vy += 0.01f;
        p->vx *= 0.999f;
        p->vy *= 0.999f;
        
        // Fade out over time
        p->life -= 0.001f;
        
        // Respawn particle if it dies or goes off screen
        if (p->life <= 0 || p->x < 0 || p->x >= ctx->screen_width || 
            p->y < 0 || p->y >= ctx->screen_height) {
            p->x = (float)(rand() % ctx->screen_width);
            p->y = (float)(rand() % ctx->screen_height);
            p->vx = ((float)rand() / RAND_MAX - 0.5f) * 0.5f;
            p->vy = ((float)rand() / RAND_MAX - 0.5f) * 0.5f;
            p->life = 1.0f;
            p->color.a = 80 + rand() % 100;
        }
        
        // Draw particle with glow
        graphics_color_t particle_color = p->color;
        particle_color.a = (uint8_t)(particle_color.a * p->life);
        
        // Draw particle center
        for (uint32_t py = 0; py < p->size; py++) {
            for (uint32_t px = 0; px < p->size; px++) {
                graphics_draw_pixel((int)p->x + px, (int)p->y + py, particle_color);
            }
        }
        
        // Draw particle glow
        if (p->size > 1) {
            graphics_color_t glow = particle_color;
            glow.a /= 3;
            
            for (int gx = -1; gx <= (int)p->size; gx++) {
                for (int gy = -1; gy <= (int)p->size; gy++) {
                    if (gx < 0 || gx >= (int)p->size || gy < 0 || gy >= (int)p->size) {
                        graphics_draw_pixel((int)p->x + gx, (int)p->y + gy, glow);
                    }
                }
            }
        }
    }
}

void panicui_add_sparkle_effect(int32_t x, int32_t y) {
    // Find an empty sparkle slot
    for (int i = 0; i < MAX_SPARKLES; i++) {
        if (g_sparkles[i].alpha <= 0) {
            g_sparkles[i].x = x;
            g_sparkles[i].y = y;
            g_sparkles[i].alpha = 255;
            g_sparkles[i].creation_time = g_frame_time;
            break;
        }
    }
}

void panicui_draw_sparkles(void) {
    panicui_init_effects();
    
    // Update and draw sparkles
    for (int i = 0; i < MAX_SPARKLES; i++) {
        sparkle_t* s = &g_sparkles[i];
        
        if (s->alpha > 0) {
            // Fade out sparkle
            s->alpha -= 3;
            if (s->alpha < 0) s->alpha = 0;
            
            // Calculate sparkle animation
            uint32_t age = g_frame_time - s->creation_time;
            float sparkle_size = sin((age % 60) * M_PI / 30.0f) * 3 + 2;
            
            graphics_color_t sparkle_color = {255, 255, 200, (uint8_t)s->alpha};
            
            // Draw sparkle as a cross
            graphics_draw_line((int)(s->x - sparkle_size), (int)s->y, 
                              (int)(s->x + sparkle_size), (int)s->y, sparkle_color);
            graphics_draw_line((int)s->x, (int)(s->y - sparkle_size), 
                              (int)s->x, (int)(s->y + sparkle_size), sparkle_color);
            
            // Draw center point brighter
            graphics_color_t center_color = sparkle_color;
            center_color.a = (uint8_t)(center_color.a * 1.5f);
            graphics_draw_pixel((int)s->x, (int)s->y, center_color);
        }
    }
}

void panicui_draw_scanlines(void) {
    panicui_context_t* ctx = panicui_get_context();
    if (!ctx) return;
    
    // Draw subtle scanlines for retro CRT effect
    graphics_color_t scanline_color = {0, 0, 0, 20};
    
    for (uint32_t y = 0; y < ctx->screen_height; y += 2) {
        graphics_rect_t line = {0, y, ctx->screen_width, 1};
        graphics_draw_rect(&line, scanline_color, true);
    }
}

void panicui_draw_vignette(void) {
    panicui_context_t* ctx = panicui_get_context();
    if (!ctx) return;
    
    // Draw vignette effect (darker edges)
    uint32_t center_x = ctx->screen_width / 2;
    uint32_t center_y = ctx->screen_height / 2;
    uint32_t max_dist = (ctx->screen_width + ctx->screen_height) / 4;
    
    for (uint32_t y = 0; y < ctx->screen_height; y += 4) {
        for (uint32_t x = 0; x < ctx->screen_width; x += 4) {
            float dx = (float)(x - center_x);
            float dy = (float)(y - center_y);
            float dist = sqrt(dx * dx + dy * dy);
            
            if (dist > max_dist * 0.6f) {
                uint8_t alpha = (uint8_t)((dist - max_dist * 0.6f) / (max_dist * 0.4f) * 100);
                if (alpha > 100) alpha = 100;
                
                graphics_color_t vignette_color = {0, 0, 0, alpha};
                graphics_rect_t pixel = {x, y, 4, 4};
                graphics_draw_rect(&pixel, vignette_color, true);
            }
        }
    }
}

void panicui_render_enhanced_frame(void) {
    panicui_context_t* ctx = panicui_get_context();
    if (!ctx || !ctx->enable_animations) return;
    
    // Draw animated background first
    panicui_draw_animated_background();
    
    // Draw floating particles
    panicui_draw_particle_system();
    
    // Normal panic UI rendering happens here...
    
    // Draw overlay effects
    panicui_draw_sparkles();
    panicui_draw_scanlines();
    panicui_draw_vignette();
}

#endif /* ARCH_64BIT */
