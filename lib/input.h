/**
 * input.h - A safer implementation of scanf-like functionality
 *
 * This implementation focuses on safety against:
 * - Buffer overflows
 * - NULL pointer dereferences
 * - Memory leaks
 * - Integer overflows
 * - Format string vulnerabilities
 */

#ifndef INPUT_H
#define INPUT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <limits.h>

// Return codes for input functions
typedef enum
{
    INPUT_SUCCESS = 0,
    INPUT_NULL_PTR = -1,
    INPUT_INVALID_FORMAT = -2,
    INPUT_BUFFER_OVERFLOW = -3,
    INPUT_CONVERSION_ERROR = -4,
    INPUT_IO_ERROR = -5,
    INPUT_SHORT_OVERFLOW = -6,
    INPUT_INTEGER_OVERFLOW = -7,
    INPUT_FLOAT_OVERFLOW = -8,
    INPUT_DOUBLE_OVERFLOW = -9,
    INPUT_INVALID_LENGTH = -10,
} input_status;

/**
 * Clears the remaining input in stdin to prevent it from affecting subsequent reads.
 */
void clear_stdin_buffer(void);

/**
 * Safely reads a single character value
 *
 * @param value Pointer to store the character value
 * @return input_status indicating success or type of error
 */
input_status input_char(char *value);

/**
 * Safely reads a string with a maximum length
 *
 * @param buffer Pointer to the buffer where the string will be stored
 * @param buffer_size Size of the buffer in bytes
 * @param chars_read Pointer to store the number of characters read
 * @return input_status indicating success or type of error
 */
input_status input_string(char *buffer, size_t buffer_size, size_t *chars_read);

/**
 * Safely reads an short value
 *
 * @param value Pointer to store the short value
 * @return input_status indicating success or type of error
 */
input_status input_short(short *value);

/**
 * Safely reads an integer value
 *
 * @param value Pointer to store the integer value
 * @return input_status indicating success or type of error
 */
input_status input_int(int *value);

/**
 * Safely reads a float value
 *
 * @param value Pointer to store the float value
 * @return input_status indicating success or type of error
 */
input_status input_float(float *value);

/**
 * Safely reads a double value
 *
 * @param value Pointer to store the double value
 * @return input_status indicating success or type of error
 */
input_status input_double(double *value);

#endif // INPUT_H
