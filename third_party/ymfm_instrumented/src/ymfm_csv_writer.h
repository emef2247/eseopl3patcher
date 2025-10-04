#ifndef YMFM_CSV_WRITER_H
#define YMFM_CSV_WRITER_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/**
 * Lightweight CSV event writer for OPLL register write logging.
 * 
 * Phase 1 columns:
 *   time_s,sample,chip,addr,data,#type,ch,ko,blk,fnum,fnumL,inst,vol
 * 
 * Usage:
 *   csv_writer_t writer;
 *   csv_writer_init(&writer, "output.csv", sample_rate);
 *   csv_writer_write_header(&writer);
 *   csv_writer_write_event(&writer, ...);
 *   csv_writer_close(&writer);
 */

typedef struct {
    FILE *fp;
    double sample_rate;
    bool is_open;
} csv_writer_t;

/**
 * Initialize CSV writer. Opens file for writing.
 * Returns true on success, false on error.
 */
static inline bool csv_writer_init(csv_writer_t *writer, const char *filename, double sample_rate) {
    if (!writer) return false;
    
    writer->fp = fopen(filename, "w");
    writer->sample_rate = sample_rate;
    writer->is_open = (writer->fp != NULL);
    
    return writer->is_open;
}

/**
 * Write CSV header row.
 */
static inline bool csv_writer_write_header(csv_writer_t *writer) {
    if (!writer || !writer->is_open || !writer->fp) return false;
    
    fprintf(writer->fp, "time_s,sample,chip,addr,data,#type,ch,ko,blk,fnum,fnumL,inst,vol\n");
    fflush(writer->fp);
    
    return true;
}

/**
 * Write an OPLL register write event.
 * 
 * Parameters:
 *   sample_index: Current sample counter
 *   addr: Register address (0x10-0x38 typically)
 *   data: Register data value
 *   type_str: Event type string ("fL", "fHBK", "iv")
 *   ch: Channel number (0-8)
 *   ko: Key on bit (0 or 1)
 *   blk: Block/octave value (0-7)
 *   fnum: Full F-number (0-511)
 *   fnumL: Lower 8 bits of F-number
 *   inst: Instrument number (0-15)
 *   vol: Volume (0-15)
 */
static inline bool csv_writer_write_event(csv_writer_t *writer,
                                          uint64_t sample_index,
                                          uint8_t addr,
                                          uint8_t data,
                                          const char *type_str,
                                          int ch,
                                          int ko,
                                          int blk,
                                          int fnum,
                                          int fnumL,
                                          int inst,
                                          int vol) {
    if (!writer || !writer->is_open || !writer->fp) return false;
    
    double time_s = (double)sample_index / writer->sample_rate;
    
    // time_s,sample,chip,addr,data,#type,ch,ko,blk,fnum,fnumL,inst,vol
    fprintf(writer->fp, "%.6f,%llu,YM2413,0x%02X,0x%02X,%s,%d,%d,%d,%d,%d,%d,%d\n",
            time_s,
            (unsigned long long)sample_index,
            addr,
            data,
            type_str,
            ch,
            ko,
            blk,
            fnum,
            fnumL,
            inst,
            vol);
    
    fflush(writer->fp);
    
    return true;
}

/**
 * Close the CSV writer and flush all data.
 */
static inline void csv_writer_close(csv_writer_t *writer) {
    if (!writer) return;
    
    if (writer->is_open && writer->fp) {
        fflush(writer->fp);
        fclose(writer->fp);
        writer->fp = NULL;
    }
    
    writer->is_open = false;
}

#endif /* YMFM_CSV_WRITER_H */
