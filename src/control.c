// -----------------------------------------------------------------------------
// controller - the glue between the engine and the hardware
//
// reacts to events (grid press, clock etc) and translates them into appropriate
// engine actions. reacts to engine updates and translates them into user 
// interface and hardware updates (grid LEDs, CV outputs etc)
//
// should talk to hardware via what's defined in interface.h only
// should talk to the engine via what's defined in engine.h only
// ----------------------------------------------------------------------------

#include "compiler.h"
#include "string.h"

#include "control.h"
#include "interface.h"
#include "engine.h"

preset_meta_t meta;
preset_data_t preset;
shared_data_t shared;
int selected_preset;

// ----------------------------------------------------------------------------
// firmware dependent stuff starts here

#define MAXX 16
#define MAXY 16
#define SEED_CELL_COUNT 16
#define MAX_RESEED_DIV 1
#define MAX_WAVE_REPEAT 32

/*

|||||||| one wave is 8 columns
we repeat wave 16 times, then generate next wave

*/

static u8 live_min = 2;
static u8 live_max = 3;
static u8 birth_min = 2;
static u8 birth_max = 3;

static u8 reseed_div = MAX_RESEED_DIV;
static u16 wave_repeat = MAX_WAVE_REPEAT;
static u8 gen, cv_update_count, seed_count, wave_count;
static u8 states[MAXX][MAXY][2];
static u8 seed_x[SEED_CELL_COUNT], seed_y[SEED_CELL_COUNT];

static u8 neighbours(uint8_t x, uint8_t y, uint8_t gen);
static void next_gen(void);
static void play(void);
static void seed(void);
static void visualize(void);
static void update_cv(void);
static void grid_press(u8 x, u8 y, u8 pressed);
static void arc_turn(u8 enc, u8 dir);
static void check_knobs(void);


// https://llllllll.co/t/multipass-a-framework-for-developing-firmwares-for-monome-eurorack-modules/26354/67

// ----------------------------------------------------------------------------
// functions for main.c

void init_presets(void) {
    // called by main.c if there are no presets saved to flash yet
    // initialize meta - some meta data to be associated with a preset, like a glyph
    // initialize shared (any data that should be shared by all presets) with default values
    // initialize preset with default values 
    // store them to flash
    
    for (u8 i = 0; i < get_preset_count(); i++) {
        store_preset_to_flash(i, &meta, &preset);
    }

    store_shared_data_to_flash(&shared);
    store_preset_index(0);
}

void init_control(void) {
    // load shared data
    // load current preset and its meta data
    
    load_shared_data_from_flash(&shared);
    selected_preset = get_preset_index();
    load_preset_from_flash(selected_preset, &preset);
    load_preset_meta_from_flash(selected_preset, &meta);

    // set up any other initial values and timers
    add_timed_event(0, 1, 1);
}

void process_event(u8 event, u8 *data, u8 length) {
    switch (event) {
        case MAIN_CLOCK_RECEIVED:
            break;
        
        case MAIN_CLOCK_SWITCHED:
            break;
    
        case GATE_RECEIVED:
            break;
        
        case GRID_CONNECTED:
            break;
        
        case GRID_KEY_PRESSED:
            grid_press(data[0], data[1], data[2]);
            break;
    
        case GRID_KEY_HELD:
            break;
            
        case ARC_ENCODER_COARSE:
            arc_turn(data[0], data[1]);
            break;
    
        case FRONT_BUTTON_PRESSED:
            break;
    
        case FRONT_BUTTON_HELD:
            break;
    
        case BUTTON_PRESSED:
            break;
    
        case I2C_RECEIVED:
            break;
            
        case TIMED_EVENT:
            play();
            break;
        
        case MIDI_CONNECTED:
            break;
        
        case MIDI_NOTE:
            break;
        
        case MIDI_CC:
            break;
            
        case MIDI_AFTERTOUCH:
            break;
            
        case SHNTH_BAR:
            break;
            
        case SHNTH_ANTENNA:
            break;
            
        case SHNTH_BUTTON:
            break;
            
        default:
            break;
    }
}

void render_grid(void) {
    // render grid LEDs here or leave blank if not used
}

void render_arc(void) {
    // render arc LEDs here or leave blank if not used
}

u8 neighbours(uint8_t x, uint8_t y, uint8_t gen) {
    return 
        (states[(x+MAXX-1)%MAXX][(y+MAXY-1)%MAXY][gen] > 0) +
        (states[(x+0)%MAXX][(y+MAXY-1)%MAXY][gen] > 0) +
        (states[(x+1)%MAXX][(y+MAXY-1)%MAXY][gen] > 0) +
        (states[(x+MAXX-1)%MAXX][(y+0)%MAXY][gen] > 0) +
        (states[(x+1)%MAXX][(y+0)%MAXY][gen] > 0) +
        (states[(x+MAXX-1)%MAXX][(y+1)%MAXY][gen] > 0) +
        (states[(x+0)%MAXX][(y+1)%MAXY][gen] > 0) +
        (states[(x+1)%MAXX][(y+1)%MAXY][gen] > 0);
}

void play() {
    if (!cv_update_count) {
        if (++wave_count >= wave_repeat) {
            next_gen();
            wave_count = 0;
        }
    }
    
    update_cv();
    // check_knobs();
}

void next_gen() {
    int count = 0;
    uint8_t nextgen = gen ? 0 : 1;
    
    for (int x = 0; x < MAXX; x++)
        for (int y = 0; y < MAXY; y++) {
            uint8_t n = neighbours(x, y, gen);
            if (states[x][y][gen] && (n >= live_min && n <= live_max)) {
                states[x][y][nextgen] = 1;
                count++;
            } else if (!states[x][y][gen] && (n >= birth_min && n <= birth_max)) {
                states[x][y][nextgen] = 1;
                count++;
            } else {
                states[x][y][nextgen] = 0;
            }
        }

    gen = nextgen;
    if (!count) seed();
    
    visualize();
}

void seed() {
    //if (++seed_count >= reseed_div) {
        seed_x[0] = rand() % MAXX;
        seed_y[0] = rand() % MAXY;
        
        for (int i = 1; i < SEED_CELL_COUNT; i++) {
            seed_x[i] = (seed_x[i-1] + (rand() & 1 ? 1 : MAXX - 1)) % MAXX;
            seed_y[i] = (seed_y[i-1] + (rand() & 1 ? 1 : MAXY - 1)) % MAXY;
        }
        
        seed_count = 0;
    //}
    
    for (int i = 0; i < SEED_CELL_COUNT; i++) states[seed_x[i]][seed_y[i]][gen] = 1;
}

void visualize() {
    clear_all_grid_leds();
    
    int maxx = min(MAXX, get_grid_column_count());
    int maxy = min(MAXY, get_grid_row_count());
    
    for (int x = 0; x < maxx; x++)
        for (int y = 0; y < maxy; y++)
            set_grid_led(x, y, states[x][y][gen] ? 8 : 0);
        
    /*
    for (int x = 0; x < min(16, live_max); x++) set_grid_led(x, 0, 15);
    for (int x = 0; x < min(16, birth_min); x++) set_grid_led(x, 1, 15);
    for (int x = 0; x < min(16, birth_max); x++) set_grid_led(x, 7, 15);
    */
    
    refresh_grid();
    
    clear_all_arc_leds();
    u16 index;
    for (int x = 0; x < 16; x++)
        for (int y = 0; y < 16; y++) {
            index = x * y;
            set_arc_led(index >> 6, index & 0b111111, states[x][y][gen] ? 4 : 0);
        }
        
    if (live_max) for (int i = (live_max - 1) * 8; i < live_max * 8; i++) set_arc_led(0, i, 15);
    if (birth_min) for (int i = (birth_min - 1) * 8; i < birth_min * 8; i++) set_arc_led(1, i, 15);
    if (birth_max) for (int i = (birth_max - 1) * 8; i < birth_max * 8; i++) set_arc_led(2, i, 15);
    if (birth_max) for (int i = (wave_repeat - 1) * (64 / MAX_WAVE_REPEAT); i < wave_repeat * (64 / MAX_WAVE_REPEAT); i++) set_arc_led(3, i, 15);
    
    refresh_arc();
}

void update_cv() {
    if (++cv_update_count >= MAXY) cv_update_count = 0;
    
    u16 total = 0;
    for (int x = 0; x < MAXX; x++) total += states[x][cv_update_count][gen];
    set_cv(0, (MAX_LEVEL / MAXX) * total);
}

void grid_press(u8 x, u8 y, u8 pressed) {
    if (pressed) states[x][y][gen] = 1;
    visualize();
}

void check_knobs() {
    /*
    u32 v = (get_knob_value(0) * MAX_WAVE_REPEAT) / MAX_LEVEL + 1;
    wave_repeat = v;
    v = (get_knob_value(1) * MAX_RESEED_DIV) / MAX_LEVEL + 1;
    reseed_div = v;
    v = (get_knob_value(2) * 9) / MAX_LEVEL + 1;
    */
    
    /*
    u32 v = (get_knob_value(0) * 9) / 65535 + 1;
    birth_min = v;
    v = (get_knob_value(1) * 9) / 65535 + 1;
    birth_max = v;
    v = (get_knob_value(2) * MAX_WAVE_REPEAT) / 65535 + 1;
    wave_repeat = v;
    visualize();
    */
}

void arc_turn(u8 enc, u8 dir) {
    if (enc == 0) {
        if (dir && live_max < 8) live_max++; else if (!dir && live_max > 0) live_max--;
    } else if (enc == 1) {
        if (dir && birth_min < 8) birth_min++; else if (!dir && birth_min > 0) birth_min--;
    } else if (enc == 2) {
        if (dir && birth_max < 8) birth_max++; else if (!dir && birth_max > 0) birth_max--;
    } else if (enc == 3) {
        if (dir && wave_repeat < MAX_WAVE_REPEAT) wave_repeat++; else if (!dir && wave_repeat > 0) wave_repeat--;
        seed();
    }
    visualize();
}