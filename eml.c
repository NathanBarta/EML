#include "eml.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// #define EML_PARSER_VERSION "0.0.0"
// #define DEBUG

/*
 * The maximum length a user-input string may be (excluding sentinel)
 */
#define MAX_NAME_LENGTH 128

#define true 1
#define false 0

/* 
 * Parser Parameters:
 * version - the version in the eml header
 * weight - the weight abbreviation in the eml header
 */ 
static char version[13];
static char weight[4];

/*
 * Global Parser Vars:
 * emlString - The eml to be parsed
 * emlstringlen - The length of the emlString (excluding sentinel)
 * current_position - The index of emlString the parser is currently on
 */
static char *emlString;
static int emlstringlen;
static int current_postition;

/*
 * Parser Output:
 * result - Parsed eml header and objects
 */
static eml_result *result = NULL;

int parse(char *eml_string);
static int parse_header();

static void validate_header_t(eml_header_t *h);

static int parse_string(char **result);

static int parse_header_t(eml_header_t **tht);
static int parse_super_t(eml_super_t **tsupt);
static int parse_single_t(eml_single_t **tst);

static int flush(eml_single_t *tst, uint32_t *vcount, eml_kind_flag kind, eml_modifier_flag mod, eml_number *buf, uint32_t *dcount);
static void move_to_asymmetric(eml_single_t *tst, bool side);
static int upgrade_to_asymmetric(eml_single_t *tst);
static int upgrade_to_standard_varied(eml_single_t *tst);
static int upgrade_to_standard(eml_single_t *tst);

static char *format_eml_number(eml_number *e);
static void print_standard_k(eml_standard_k *k);
static void print_standard_varied_k(eml_standard_varied_k *k);
static void print_single_t(eml_single_t *s);
static void print_super_t(eml_super_t *s);
static void print_emlobj(eml_obj *e);

static void free_single_t(eml_single_t *s);
static void free_super_t(eml_super_t *s);
static void free_emlobj(eml_obj *e);
static void free_result();

/*
 * parse: Entry point for parsing eml. Starts at '{', ends at (emlstringlen - 1)
 */
int parse(char *eml_string) {
    emlString = eml_string;
    emlstringlen = strlen(eml_string);
    current_postition = 0;

    result = malloc(sizeof(eml_result));
    if (result == NULL) {
        return allocation_error;
    }

    eml_obj *obj_tail = NULL;

    #ifdef DEBUG
        printf("EML String: %s, length: %i\n", eml_string, emlstringlen);
    #endif

    eml_super_t *tsupt = NULL;
    eml_single_t *tst = NULL;
    int error = 0;

    while (current_postition < emlstringlen) {
        char current = emlString[current_postition];

        switch (current) {
        case (int)'{': // Give control to parse_header()
            parse_header();

            #ifdef DEBUG
                printf("parsed version: %s, parsed weight: %s\n", version, weight);
                printf("-------------------------\n");
            #endif

            break;
        case (int)'s': // Give control to parse_super_t()
            if ((error = parse_super_t(&tsupt))) {
                goto bail;
            }

            eml_obj *temp_super = malloc(sizeof(eml_obj));
            if (temp_super == NULL) {
                error = allocation_error;
                goto bail;
            }
            temp_super->type = super;
            temp_super->data = tsupt;
            temp_super->next = NULL;

            if (result->objs == NULL) {
                result->objs = temp_super;
            } else {
                obj_tail->next = temp_super;
            }

            obj_tail = temp_super;
            break;
        case (int)'c': // Give control to parse_super_t()
            if ((error = parse_super_t(&tsupt))) {
                goto bail;
            }

            eml_obj *temp_circuit = malloc(sizeof(eml_obj));
            if (temp_circuit == NULL) {
                error = allocation_error;
                goto bail;
            }
            temp_circuit->type = circuit;
            temp_circuit->data = tsupt;
            temp_circuit->next = NULL;

            if (result->objs == NULL) {
                result->objs = temp_circuit;
            } else {
                obj_tail->next = temp_circuit;
            }

            obj_tail = temp_circuit;
            break;
        case (int)'\"': // Give control to parse_single_t() 
            if ((error = parse_single_t(&tst))) {
                goto bail;
            }

            eml_obj *temp_single = malloc(sizeof(eml_obj));
            if (temp_single == NULL) {
                error = allocation_error;
                goto bail;
            }
            temp_single->type = single;
            temp_single->data = tst;
            temp_single->next = NULL;

            if (result->objs == NULL) {
                result->objs = temp_single;
            } else {
                obj_tail->next = temp_single;
            }

            obj_tail = temp_single;
            break;
        case (int)';':
            ++current_postition;
            break;
        default:
            error = unexpected_error;
            goto bail;
        }
    }

    return no_error;

    bail:
        if (tst != NULL) {
            free_single_t(tst);
        }

        if (tsupt != NULL) {
            free_super_t(tsupt);
        }

        free_result();
        return error;
}

/*
 * parse_header: Parses header section or exits. Starts on "{" of header, ends on char succeeding "}"
*/
static int parse_header() {
    eml_header_t *tht = NULL;
    int error = no_error;

    if (emlString[current_postition++] != (int)'{') {
        error = missing_header_start_char;
        return error;
    }

    while (current_postition < emlstringlen) {
        char current = emlString[current_postition];

        switch (current){
        case (int)'}': // Release control & inc
            ++current_postition;
            return no_error;
        case (int)',':
            ++current_postition;
            break;
        case (int)'\"':
            if ((error = parse_header_t(&tht))) {
                return error;
            }
            validate_header_t(tht);
            tht->next = result->header;
            result->header = tht;
            break;
        default:
            return unexpected_error;
        }
    }

    error = unexpected_error;

    return error;
}

/*
 * parse_header_t: Returns an eml_header_t or exits. Starts on '"', ends on ',' or '}'.
*/
static int parse_header_t(eml_header_t **tht) {
    *tht = malloc(sizeof(eml_header_t));
    if (*tht == NULL) {
        return allocation_error;
    }
    
    bool pv = false; // Toggle between parameter & value

    int error = no_error;

    while (current_postition < emlstringlen) {
        char current = emlString[current_postition];

        switch (current) {
            case (int)'}': // Release control
                return no_error;
            case (int)',': // Release control
                return no_error;
            case (int)':':
                pv = true;
                ++current_postition;
                break;
            case (int)'\"':
                if (pv == false) {
                    if ((error = parse_string(&(*tht)->parameter))) {
                        goto bail;
                    }
                }
                else {
                    if ((error = parse_string(&(*tht)->value))) {
                        goto bail;
                    }
                }
                break;
            default:
                error = unexpected_error;
                goto bail;
        }
    }

    error = unexpected_error;

    bail:
        if (*tht != NULL) {
            if ((*tht)->parameter != NULL) {
                free((*tht)->parameter);
            }

            if ((*tht)->value != NULL) {
                free((*tht)->value);
            }

            free(*tht);
        }

        return error;
}

/*
 * parse_super_t: Returns an eml_super_t or exits. Starts on 's', ends succeeding ')'.
*/
static int parse_super_t(eml_super_t **tsupt) {
    *tsupt = malloc(sizeof(eml_super_t));
    if (tsupt == NULL) {
        return allocation_error;
    } 

    eml_single_t *tst = NULL;
    eml_super_member_t *set_tail = NULL;

    int error = no_error;

    while (current_postition < emlstringlen) {
        char current = emlString[current_postition];

        switch (current) {
            case (int)'(':
                ++current_postition;
                break;
            case (int)'\"':
                if ((error = parse_single_t(&tst))) {
                    return error;
                }

                eml_super_member_t *temp = malloc(sizeof(eml_super_member_t));
                temp->single = tst;
                temp->next = NULL;

                if ((*tsupt)->sets == NULL) {
                    (*tsupt)->sets = temp;
                } else {
                    set_tail->next = temp;
                }

                (*tsupt)->count++;
                set_tail = temp;
                break;
            case (int)')':
                ++current_postition;
                return no_error;
            default: // skip {'u', 'p', 'e', 'r'} and {'i', 'r', 'c', 'u', 'i', 't'}
                ++current_postition;
                break;
        }
    }

    error = unexpected_error;

    return error;
}

/*
 * parse_single_t: Returns an eml_single_t or exits. Starts on '"', ends succeeding ';'
*/
static int parse_single_t(eml_single_t **tst) {
    *tst = malloc(sizeof(eml_single_t));
    if (tst == NULL) {
        return allocation_error;
    } 

    // Initialize eml_single_t
    (*tst)->name = NULL;
    (*tst)->no_work = NULL;
    (*tst)->standard_work = NULL;
    (*tst)->standard_varied_work = NULL;
    (*tst)->asymmetric_work = NULL;

    int error = no_error;
    // #define BAIL(e) { error = e; goto bail;}

    if ((error = parse_string(&(*tst)->name))) {
        goto bail;
    }

    eml_kind_flag kind = none;
    eml_modifier_flag modifier = no_mod; 

    eml_number buffer_int = 0;  // Rolling eml_number
    uint32_t dcount = 0;        // If >0, writing to (dcount * 10)'ths place, >2 error
    uint32_t vcount = 0;        // Index of standard_varied_k.vReps[]

    uint32_t temp;              // Used for building eml_number in `default`

    if (emlString[current_postition++] != (int)':') {
        error = name_work_separator_error;
        goto bail;
    }

    while (current_postition < emlstringlen) {
        char current = emlString[current_postition];

        switch (current) {
        case (int)'\"':
            ++current_postition;
            break;
        case (int)':':
            // Upgrade to asymetric_k
            // Write value/modifier. If kind == standard_varied_work, write as macro.
            if ((error = flush(*tst, NULL, kind, modifier, &buffer_int, &dcount))) {
                goto bail;
            } 

            modifier = no_mod;
            kind = none;

            // Upgrade existing eml_single_t to asymmetric
            if ((error = upgrade_to_asymmetric(*tst))) {
                goto bail;
            }
            
            ++current_postition;
            break;
        case (int)'x':
            // Allocate standard_work & set sets.
            if ((error = upgrade_to_standard(*tst))) {
                goto bail;
            }
            (*tst)->standard_work->sets = buffer_int;

            buffer_int = 0;
            kind = standard;

            ++current_postition;
            break;
        case (int)'(':
            // Allocate standard_varied_work & dealloc/transition standard_work
            if ((error = upgrade_to_standard_varied(*tst))) {
                goto bail;
            }

            vcount = 0;
            kind = standard_varied;

            ++current_postition;
            break;
        case (int)',':
            if (vcount > (*tst)->standard_varied_work->sets) {
                error = extra_variable_reps_error;
                goto bail;
            }

            // Write reps/(internal)modifiers
            if ((error = flush(*tst, &vcount, kind, modifier, &buffer_int, &dcount))) {
                goto bail;
            }

            vcount++;
            modifier = no_mod;

            ++current_postition;
            break;
        case (int)')':
            if (vcount > (*tst)->standard_varied_work->sets) {
                error = extra_variable_reps_error;
                goto bail;
            }

            // Write reps/(internal)modifiers
            if ((error = flush(*tst, &vcount, kind, modifier, &buffer_int, &dcount))) {
                goto bail;
            }

            vcount++;
            modifier = no_mod;

            if (vcount < (*tst)->standard_varied_work->sets) {
                error = missing_variable_reps_error;
                goto bail;
            }
            ++current_postition;
            break;
        case (int)'F':
            switch (kind) {
                case none:
                    error = none_work_to_failure_error;
                    goto bail;
                case standard:
                    (*tst)->standard_work->reps.toFailure = true;
                    break;
                case standard_varied:
                    // Apply 'toFailure' to internal Reps
                    if (vcount < (*tst)->standard_varied_work->sets) {
                        (*tst)->standard_varied_work->vReps[vcount].toFailure = true;
                    }
                    else {
                        error = to_failure_used_as_macro_error;
                        goto bail;
                    }
                    break;
            }

            ++current_postition;
            break;
        case (int)'T':
            switch (kind) {
                case none:
                    error = modifier_on_none_work_error;
                    goto bail;
                case standard:
                    (*tst)->standard_work->reps.isTime = true;
                    break;
                case standard_varied:
                    // Apply 'isTime' to internal Reps
                    if (vcount < (*tst)->standard_varied_work->sets) {
                        (*tst)->standard_varied_work->vReps[vcount].isTime = true;
                    }
                    else {
                        error = time_macro_error;
                        goto bail;
                    }
                    break;
            }

            ++current_postition;
            break;
        case (int)'@':
            switch (kind) {
                case none:
                    error = modifier_on_none_work_error;
                    goto bail;
                case standard:
                    (*tst)->standard_work->reps.value = buffer_int;
                    break;
                case standard_varied:
                    // Apply weight modifier to internal Reps (EX: 3x(5@120,...,...))
                    if (vcount < (*tst)->standard_varied_work->sets) {
                        (*tst)->standard_varied_work->vReps[vcount].value = buffer_int;
                    }
                    break;
            }

            buffer_int = 0;
            dcount = 0;
            modifier = weight_mod;
            
            ++current_postition;
            break;
        case (int)'%':
            switch (kind) {
                case none:
                    error = modifier_on_none_work_error;
                    goto bail;
                case standard:
                    (*tst)->standard_work->reps.value = buffer_int;
                    break;
                case standard_varied:
                    // Apply weight modifier to internal Reps (EX: 3x(5%120,...,...))
                    if (vcount < (*tst)->standard_varied_work->sets) {
                        (*tst)->standard_varied_work->vReps[vcount].value = buffer_int;
                    }
                    break;
            }

            buffer_int = 0;
            dcount = 0;
            modifier = rpe_mod;
            
            ++current_postition;
            break;
        case (int)'.':
            if (kind == none) {
                error = fractional_sets_error;
                goto bail;
            }

            if (dcount) {
                error = multiple_radix_points_error;
                goto bail;
            }

            if (modifier == no_mod) {
                error = fractional_none_modifier_value_error;
                goto bail;
            }

            // Set H bit, potential overflow handled in `default`
            buffer_int = buffer_int * 100U | eml_number_H; 

            ++dcount;
            ++current_postition;
            break;
        case (int)';':
            // Write value/modifier. If kind == standard_varied_work, write as macro.
            if ((error = flush(*tst, NULL, kind, modifier, &buffer_int, &dcount))) {
                goto bail;
            }

            if ((*tst)->asymmetric_work != NULL) {
                move_to_asymmetric(*tst, 1);
            }

            ++current_postition;
            return no_error; // Give control back
        default:
            switch (dcount) {
                case 0: // Before radix
                    temp = buffer_int * 10U + (unsigned int)current - '0';

                    if (temp > 21474835U) { 
                        error = integral_overflow_error;
                        goto bail;
                    }

                    buffer_int = temp;
                    break;
                case 1: // 10ths place
                    temp = buffer_int + ((unsigned int)current - '0') * 10U;

                    if ((temp & eml_number_mask) > 2147483500U) {
                        error = fp_overflow_error;
                        goto bail;
                    }

                    buffer_int = temp;
                    dcount++;
                    break;
                case 2: // 100ths place
                    temp = buffer_int + ((unsigned int)current - '0');

                    if ((temp & eml_number_mask) > 2147483500U) {
                        error = fp_overflow_error;
                        goto bail;
                    }

                    buffer_int = temp;
                    dcount++;
                    break;
                default:
                    error = too_many_fp_digits;
                    goto bail;
            }
            
            ++current_postition;
            break;
        }
    }

    error = unexpected_error;

    bail:
        free_single_t(*tst);
        return error;
}

static void validate_header_t(eml_header_t *h) {
    if (strcmp(h->parameter, "version") == 0) {
        strncpy(version, h->value, 12);
        version[11] = '\0';
    }

    if (strcmp(h->parameter, "weight") == 0) {
        strncpy(weight, h->value, 4);
        weight[3] = '\0';
    }
}

/*
 * parse_string: Returns a string (char*) or exits. Starts on '"', ends succeeding the next '"'
 */
static int parse_string(char **result) {
    static char strbuf[MAX_NAME_LENGTH + 1];
    uint32_t strindex = 0;

    ++current_postition; // skip '"'
    while (current_postition < emlstringlen) {
        char current = emlString[current_postition];

        switch (current) {
            case (int)'\"':
                ++current_postition;

                if (strindex == 0) {
                    return empty_string_error;
                }

                *result = malloc(strindex + 1);
                if (*result == NULL) {
                    return allocation_error;
                }
            
                strncpy(*result, strbuf, MAX_NAME_LENGTH + 1);
                (*result)[strindex] = '\0';
                return no_error;
            default:
                if (strindex > 127) {
                    free(*result);
                    return string_length_error;
                }

                strbuf[strindex++] = current;
                ++current_postition;
                break;
        }
    }

    return no_error;
}

/* 
 * flush: Writes buf to the appropriate field in eml_single_t. If there is none work, flush will malloc tst->no_work. 
 *        If `vcount` is NULL, modifier will be applied as a macro. Resets `buf` and `dcount`.
 */
static int flush(eml_single_t *tst, uint32_t *vcount, eml_kind_flag kind, eml_modifier_flag mod, eml_number *buf, uint32_t *dcount) {
    if (*dcount == 1) {
        return missing_digit_following_radix_error;
    }

    switch (kind) {
        case none:
            switch (mod) {
                case no_mod:
                    tst->no_work = malloc(sizeof(bool));
                    if (tst->no_work == NULL) {
                        return allocation_error;
                    }

                    break;
                default:
                    return modifier_on_none_work_error;
            }
            break;
        case standard:
            switch (mod) {
                case no_mod:
                    tst->standard_work->reps.value = *buf; 
                    break;
                case weight_mod:
                    tst->standard_work->reps.mod = weight_mod;
                    tst->standard_work->reps.modifier.weight = *buf; 
                    break;
                case rpe_mod:
                    tst->standard_work->reps.mod = rpe_mod;
                    tst->standard_work->reps.modifier.rpe = *buf;
                    break;
            }
            break;
        case standard_varied:
            if (vcount == NULL) { // MACRO MODIFIER
                switch (mod) {
                    case no_mod:
                        break;
                    case weight_mod:
                        for (int i = 0; i < tst->standard_varied_work->sets; i++) {
                            if (tst->standard_varied_work->vReps[i].mod == no_mod) {
                                tst->standard_varied_work->vReps[i].mod = weight_mod;
                                tst->standard_varied_work->vReps[i].modifier.weight = *buf;
                            }
                        }
                        break;
                    case rpe_mod:
                        for (int i = 0; i < tst->standard_varied_work->sets; i++) {
                            if (tst->standard_varied_work->vReps[i].mod == no_mod) {
                                tst->standard_varied_work->vReps[i].mod = rpe_mod;
                                tst->standard_varied_work->vReps[i].modifier.rpe = *buf;
                            }
                        }
                        break;
                }
            } else { // INNER-MODIFIER
                switch (mod) {
                    case no_mod:
                        tst->standard_varied_work->vReps[*vcount].value = *buf;
                        break;
                    case weight_mod:
                        tst->standard_varied_work->vReps[*vcount].mod = weight_mod;
                        tst->standard_varied_work->vReps[*vcount].modifier.weight = *buf;
                        break;
                    case rpe_mod:
                        tst->standard_varied_work->vReps[*vcount].mod = rpe_mod;
                        tst->standard_varied_work->vReps[*vcount].modifier.rpe = *buf;
                        break;
                }
            }
            break;
    }

    *buf = 0;
    *dcount = 0;
    return no_error;
}

/*
 * move_to_asymmetric: Moves tst->(no_work | standard_work | standard_varied_work) kind to the left (false) or right (true) side of tst->asymmetric_work.
 */
static void move_to_asymmetric(eml_single_t *tst, bool side) {
    if (tst->no_work != NULL) {
        if (side) {
            tst->asymmetric_work->right_none_k = tst->no_work;
        } else {
            tst->asymmetric_work->left_none_k = tst->no_work;
        }
        
        tst->no_work = NULL;
    }
    else if (tst->standard_work != NULL) {
        if (side) {
            tst->asymmetric_work->right_standard_k = tst->standard_work;
        } else {
            tst->asymmetric_work->left_standard_k = tst->standard_work;
        }

        tst->standard_work = NULL;
    }
    else if (tst->standard_varied_work != NULL) {
        if (side) {
            tst->asymmetric_work->right_standard_varied_k = tst->standard_varied_work;
        } else {
            tst->asymmetric_work->left_standard_varied_k = tst->standard_varied_work;
        }

        tst->standard_varied_work = NULL;
    }
}

/*
 * upgrade_to_asymmetric: Allocates tst->asymmetric_work & moves existing tst->(no_work | standard_work | standard_varied_work) kind to left side.
 */
static int upgrade_to_asymmetric(eml_single_t *tst) {
    tst->asymmetric_work = malloc(sizeof(eml_asymmetric_k));
    if (tst->asymmetric_work == NULL) {
        return allocation_error;
    }

    tst->asymmetric_work->left_none_k = NULL;
    tst->asymmetric_work->left_standard_k = NULL;
    tst->asymmetric_work->left_standard_varied_k = NULL;
    tst->asymmetric_work->right_none_k = NULL;
    tst->asymmetric_work->right_standard_k = NULL;
    tst->asymmetric_work->right_standard_varied_k = NULL;

    move_to_asymmetric(tst, 0);
    return no_error;
}

/*
 * upgrade_to_standard_varied: Allocates tst->standard_varied_work & migrates tst->standard_work.
 */
static int upgrade_to_standard_varied(eml_single_t *tst) {
    tst->standard_varied_work = malloc(sizeof(eml_reps) * tst->standard_work->sets + sizeof(eml_number));
    if (tst->standard_varied_work == NULL) {
        return allocation_error;
    }
    
    tst->standard_varied_work->sets = tst->standard_work->sets;

    // standard_varied_kind defaults
    for (int i = 0; i < tst->standard_varied_work->sets; i++) {
        tst->standard_varied_work->vReps[i].isTime = false;
        tst->standard_varied_work->vReps[i].toFailure = false;
        tst->standard_varied_work->vReps[i].mod = no_mod;
    }

    free(tst->standard_work);
    tst->standard_work = NULL;
    return no_error;
}

/*
 * upgrade_to_standard: Allocates tst->standard_work.
 */
static int upgrade_to_standard(eml_single_t *tst) {
    tst->standard_work = malloc(sizeof(eml_standard_k));
    if (tst->standard_work == NULL) {
        return allocation_error;
    }

    tst->standard_work->reps.isTime = false;
    tst->standard_work->reps.toFailure = false;
    tst->standard_work->reps.mod = no_mod;
    return no_error;
}

/*
 * format_eml_number: Returns a temporary formatted eml_number string which is valid until next call or exits.
 */
static char *format_eml_number(eml_number *e) {
    static char f[12];

    if (*e & eml_number_H) {
        uint32_t masked = (*e & eml_number_mask);

        sprintf(f, "%u.%u", masked / 100U, masked % 100U);
    } else {
        sprintf(f, "%u", *e);
    }

    if (f[11] != '\0') { // TODO: Get rid of this / pass the error through. This function is currently only used in DEBUG mode but if it were not...
        free_result();
        exit(1);
    }

    return f;
}

/*
 * print_standard_k: Prints an eml_standard_k to stdout.
 */
static void print_standard_k(eml_standard_k *k) {
    eml_reps reps = k->reps;

    if (reps.isTime) {
        printf("%i time sets ", k->sets);

        if (reps.toFailure) {
            printf("to failure ");
        } else {
            printf("of %s seconds ", format_eml_number(&reps.value));
        }
    } else {
        printf("%i sets ", k->sets);

        if (reps.toFailure) {
            printf("to failure ");
        } else {
            printf("of %s reps ", format_eml_number(&reps.value));
        }
    }

    switch (reps.mod) {
        case no_mod:
            break;
        case weight_mod:
            printf("with %s %s", format_eml_number(&reps.modifier.weight), weight);
            break;
        case rpe_mod:
            printf("with RPE of %s", format_eml_number(&reps.modifier.rpe));
            break;
    }

    printf("\n");
}

/*
 * print_standard_varied_k: Prints an eml_standard_varied_k to stdout.
 */
static void print_standard_varied_k(eml_standard_varied_k *k) {
    int count = k->sets;
    printf("%i sets\n", count);
    for (int i = 0; i < count; i++) {
        printf(" - ");

        // standard_varied_k emulates standard_k for printing
        eml_standard_k shim;
        shim.sets = k->sets;
        shim.reps = k->vReps[i];
        print_standard_k(&shim); // FIXME: Prints X sets of ... on every line
    }
}

/*
 * print_single_t: Prints a eml_single_t to stdout.
 */
static void print_single_t(eml_single_t *s) {
    printf("--- Printing single_t ---\n");
    printf("Name: %s\n", s->name);

    if (s->no_work != NULL) {
        printf("No work\n");
    } else if (s->standard_work != NULL) {
        printf("Standard work\n");
        print_standard_k(s->standard_work);
    } else if (s->standard_varied_work != NULL) {
        printf("Standard varied work\n");
        print_standard_varied_k(s->standard_varied_work);
    } else if (s->asymmetric_work != NULL) {
        printf("Asymetric work\n");

        if (s->asymmetric_work->left_none_k != NULL) {
            printf("LEFT: No work\n");
        } else if (s->asymmetric_work->left_standard_k != NULL) {
            printf("LEFT: Standard work ");
            print_standard_k(s->asymmetric_work->left_standard_k);
        } else if (s->asymmetric_work->left_standard_varied_k != NULL) {
            printf("LEFT: Standard varied work ");
            print_standard_varied_k(s->asymmetric_work->left_standard_varied_k);
        }

        if (s->asymmetric_work->right_none_k != NULL) {
            printf("RIGHT: No work\n");
        } else if (s->asymmetric_work->right_standard_k != NULL) {
            printf("RIGHT: Standard work ");
            print_standard_k(s->asymmetric_work->right_standard_k);
        } else if (s->asymmetric_work->right_standard_varied_k != NULL) {
            printf("RIGHT: Standard varied work ");
            print_standard_varied_k(s->asymmetric_work->right_standard_varied_k);
        }
    }
}

/*
 * print_super_t: Prints a eml_super_t to stdout.
 */
static void print_super_t(eml_super_t *s) {
    printf("-------------------- Super --------------------\n");
    eml_super_member_t *current = s->sets;
    while(current != NULL) {
        print_single_t(current->single);
        current = current->next;
    }
    printf("------------------ Super End ------------------\n");
    return;
}

/*
 * print_circuit_t: Prints a eml_circuit_t to stdout.
 */
static void print_circuit_t(eml_circuit_t *c) {
    printf("------------------- Circuit -------------------\n");
    eml_super_member_t *current = c->sets;
    while(current != NULL) {
        print_single_t(current->single);
        current = current->next;
    }
    printf("----------------- Circuit End -----------------\n");
    return;
}

/*
 * print_emlobj: Prints an eml_obj to stdout.
 */
static void print_emlobj(eml_obj *e) {
    switch (e->type) {
        case single:
            print_single_t((eml_single_t*) e->data);
            break;
        case super:
            print_super_t((eml_super_t*) e->data);
            break;
        case circuit:
            print_circuit_t((eml_circuit_t*) e->data);
            break;
    }
}

/*
 * free_single_t: Frees a eml_single_t.
 */
static void free_single_t(eml_single_t *s) {
    if (s->name != NULL) {
        free(s->name);
    }

    if (s->no_work != NULL) {
        free(s->no_work);
    }

    if (s->standard_work != NULL) {
        free(s->standard_work);
    }

    if (s->standard_varied_work != NULL) {
        free(s->standard_varied_work);
    }

    if (s->asymmetric_work != NULL) {
        if (s->asymmetric_work->left_none_k != NULL) {
            free(s->asymmetric_work->left_none_k);
        }

        if (s->asymmetric_work->left_standard_k != NULL) {
            free(s->asymmetric_work->left_standard_k);
        }

        if (s->asymmetric_work->left_standard_varied_k != NULL) {
            free(s->asymmetric_work->left_standard_varied_k);
        }

        if (s->asymmetric_work->right_none_k != NULL) {
            free(s->asymmetric_work->right_none_k);
        }

        if (s->asymmetric_work->right_standard_k != NULL) {
            free(s->asymmetric_work->right_standard_k);
        }

        if (s->asymmetric_work->right_standard_varied_k != NULL) {
            free(s->asymmetric_work->right_standard_varied_k);
        }

        free(s->asymmetric_work);
    }

    free(s);
}

/*
 * free_super_t: Frees a eml_super_t.
 */
static void free_super_t(eml_super_t *s) {
    eml_super_member_t *current = s->sets;
    while(current != NULL) {
        free_single_t(current->single);
        
        eml_super_member_t *t = current;
        current = current->next;
        free(t);
    }
    free(s);
}

/*
 * free_emlobj: Frees an eml_obj.
 */
static void free_emlobj(eml_obj *e) {
    if (e->type == single) {
        free_single_t((eml_single_t*) e->data);
    } else {
        free_super_t((eml_super_t*) e->data);
    }
}

/*
 * free_results: Frees all eml_header_t and eml_obj in 'result'.
 */
static void free_result() {
    if (result == NULL) {
        return;
    }

    eml_header_t *h = result->header;
    while (h != NULL) {
        result->header = h->next;

        free(h->parameter);
        free(h->value);
        free(h);
        h = result->header;
    }

    eml_obj *obj = result->objs;
    while(obj != NULL) {
        free_emlobj(obj);
        obj = obj->next;
    }
}