#ifndef ESEOPL3PATCHER_GATE_LOADER_H
#define ESEOPL3PATCHER_GATE_LOADER_H

/**
 * gate_loader.h
 * 
 * Simple C API for loading Python-estimated gate values at runtime.
 * Reads gates.csv exported by Python gate estimation tools and provides
 * lookup functions for applying gates per patch/channel.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize gate loader and load gates from CSV file.
 * 
 * CSV format: patch,channel,gate_samples
 * Example:
 *   patch,channel,gate_samples
 *   1,0,8192
 *   1,1,10240
 *   ...
 * 
 * @param csv_path Path to gates.csv file
 * @return 0 on success, negative on error
 */
int gate_loader_init(const char *csv_path);

/**
 * Lookup gate value for a specific patch and channel.
 * 
 * @param patch Patch number (0-15 for YM2413 melodic, 16-20 for rhythm)
 * @param channel Channel number (0-8 for YM2413)
 * @param out_gate Pointer to receive gate value in samples
 * @return 0 on success (found), -1 if not found
 */
int gate_loader_lookup(int patch, int channel, uint16_t *out_gate);

/**
 * Get default gate value when no specific entry exists.
 * 
 * @return Default gate value in samples (typically 8192)
 */
uint16_t gate_loader_get_default(void);

/**
 * Set default gate value.
 * 
 * @param default_gate Default gate value in samples
 */
void gate_loader_set_default(uint16_t default_gate);

/**
 * Free resources and cleanup gate loader.
 */
void gate_loader_cleanup(void);

/**
 * Get number of gate entries loaded.
 * 
 * @return Number of entries, or -1 if not initialized
 */
int gate_loader_count(void);

#ifdef __cplusplus
}
#endif

#endif /* ESEOPL3PATCHER_GATE_LOADER_H */
