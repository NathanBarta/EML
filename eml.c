#include "eml.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define true 1
#define false 0

typedef enum SectionFlag { header, body };
typedef enum WorkKindFlag { none, standard, standard_varied }; // asymetric not included since it is technically 2. Also you wouldn't attach a modifier to asymetric, just it's components
typedef enum AttachingModifierFlag { no_mod, weight_mod, rpe_mod }; // is attaching a modifier to the current work

// Parser parameters
static char version[4]; // up to 9.9
static char weight[4];

static const struct HeaderToken empty_header_t;
static const struct Reps empty_reps;
static const struct Standard empty_standard_k;
static const struct StandardVaried empty_standard_varied_k;
static const struct Asymetric empty_asymetric_k;
static const struct Single empty_single_t;
static const struct Superset empty_super_t;

void print_token(single_t s);
void print_super(super_t s);
void validate_header_t(header_t *h);
void rolling_int(char new_char, int *dest);
void parse(char* emlString);
void parse_header(char* emlString);
void parse_header_t(char* emlString, header_t* tht);
void parse_single_t(char *emlString, single_t *tst);

int emlstringlen;
int current_postition;

int main(int argc, char *argv[]){
    char emlstring[] = "{\"version\":\"1.0\",\"weight\":\"lbs\"}\"squat\":5x5;"; // standard
    // char emlstring[] = "{\"version\":\"1.0\",\"weight\":\"lbs\"}\"squat\":5x(5,4,3,2,1);"; // standard varied
    // char emlstring[] = "{\"version\":\"1.0\",\"weight\":\"lbs\"}\"sl-rdl\":4x3:5x2;"; // asymetrical standard
    // char emlstring[] = "{\"version\":\"1.0\",\"weight\":\"lbs\"}\"sl-rdl\":4x(4,3,2,1):4x(4,3,2,1);"; // asymetrical standard
    // char emlstring[] = "{\"version\":\"1.0\",\"weight\":\"lbs\"}\"sl-rdl\"::4x(4,3,2,1);"; // asymetrical mixed
    // char emlstring[] = "{\"version\":\"1.0\",\"weight\":\"lbs\"}\"squat\":;"; // none
    // char emlstring[] = "{\"version\":\"1.0\",\"weight\":\"lbs\"}\"squat\"::;"; // asymetric none

    // Multiple
    // char emlstring[] = "{\"version\":\"1.0\",\"weight\":\"lbs\"}\"squat\":5x5;\"plyo-jump\":5x40;"; // standard multiple

    // With weight modifiers
    // char emlstring[] = "{\"version\":\"1.0\",\"weight\":\"lbs\"}\"squat\":5x5@120;"; // standard + weight
    // char emlstring[] = "{\"version\":\"1.0\",\"weight\":\"lbs\"}\"squat\":4x(4,3@30,2,1)@120;"; // standard varied inner modifier & macro
    // char emlstring[] = "{\"version\":\"1.0\",\"weight\":\"lbs\"}\"sl-rdl\":4x3@60:5xF@60;"; // asymetrical standard with modifiers
    // char emlstring[] = "{\"version\":\"1.0\",\"weight\":\"lbs\"}\"sl-rdl\":4x(4,3@30,2,1)@120:3x(F,F,F)@550;"; // asymetrical mixed
    // char emlstring[] = "{\"version\":\"1.0\",\"weight\":\"lbs\"}\"squat\":;"; // none
    // char emlstring[] = "{\"version\":\"1.0\",\"weight\":\"lbs\"}\"squat\"::;"; // asymetric none

    // char emlstring[] = "{\"version\":\"1.0\",\"weight\":\"lbs\"}\"squat\":5x60T@30;"; // standard + time + weight
    // char emlstring[] = "{\"version\":\"1.0\",\"weight\":\"lbs\"}\"squat\":4x(40T,30T@550,20T,10T)@120;"; // standard varied + time + weight
    // char emlstring[] = "{\"version\":\"1.0\",\"weight\":\"lbs\"}\"sl-rdl\":4x30T@440:5x30T@72;"; // asymetrical standard + time + weight
    // char emlstring[] = "{\"version\":\"1.0\",\"weight\":\"lbs\"}\"sl-rdl\":4x(40T@770,3@30,20T,1)@120:3x(F,FT,FT)@550;"; // asymetrical mixed

    // With RPE modifiers
    // char emlstring[] = "{\"version\":\"1.0\",\"weight\":\"lbs\"}\"squat\":5x5%100;"; // standard
    // char emlstring[] = "{\"version\":\"1.0\",\"weight\":\"lbs\"}\"squat\":5x(5,4%100,3,2@40000,1)@120;"; // standard varied with modifiers and macros
    // char emlstring[] = "{\"version\":\"1.0\",\"weight\":\"lbs\"}\"sl-rdl\":4x(40T@770,3%30,20T,1)@120:3x(F%100,FT%100,FT)%80;"; // asymetrical standard + time + weight + rpe

    // Superset/Circuit
    // char emlstring[] = "{\"version\":\"1.0\",\"weight\":\"lbs\"}super(\"squat\":5x5;\"squat\":4x4;);"; // standard

    emlstringlen = strlen(emlstring);
    printf("String length: %i\n", emlstringlen);

    // super_t tsupt = empty_super_t;
    // Parser options
    // bool debug_print_current = false;

    parse(emlstring);
    printf("-------------------------\n");
    printf("parsed version: %s, parsed weight: %s\n", version, weight);


}

void parse(char* emlString) {
    current_postition = 0;

    single_t tst = empty_single_t;

    while (current_postition < emlstringlen) {
        char current = emlString[current_postition];

        switch (current){
        case (int)'{': // Give control to new_parse_header()
            parse_header(emlString);
            break;
        case (int)'s': // Give control to new_parse_super_t()
            break;
        case (int)'c': // Give control to new_parse_super_t() *probably
            break;
        case (int)'\"': // Give control to new_parse_single_t()
            parse_single_t(emlString, &tst);
            break;
        case (int)';':
            printf("End token\n");
            ++current_postition;
            break;
        case (int)'\0':
            printf("End of string\n");
            break;
        default:
            break;
        }
    }
}

// Starts on "{" of header, ends on char succeeding "}"
void parse_header(char* emlString) {
    header_t tht = empty_header_t;

    while (current_postition < emlstringlen) {
        char current = emlString[current_postition];

        switch (current){
        case (int)'{':
            current_postition++;
            break;
        case (int)'}': // Release control & inc
            validate_header_t(&tht);
            ++current_postition;
            return;
        case (int)',':
            validate_header_t(&tht);
            ++current_postition;
            break;
        case (int)'\"':
            parse_header_t(emlString, &tht); // Pass control to new_parse_header_t()
            break;
        default:
            exit(-1);
            break;
        }
    }
}

void parse_header_t(char* emlString, header_t* tht) {
    bool header_header_t_pv = false; // Toggle between parameter & value

    while (current_postition < emlstringlen) {
        char current = emlString[current_postition];

        switch (current) {
            case (int)'}': // Release control
                return;
            case (int)',': // Release control
                return;
            case (int)':':
                header_header_t_pv = true;
                ++current_postition;
                break;
            case (int)'\"':
                ++current_postition;
                break;
            default:
                if (header_header_t_pv == false) {
                    strncat(tht->parameter, &current, 1);
                }
                else {
                    strncat(tht->value, &current, 1);
                }
                ++current_postition;
                break;
        }
    }
}

void parse_single_t(char *emlString, single_t *tst) {
    enum WorkKindFlag kind = none;
    enum AttachingModifierFlag modifier = no_mod; 
    bool body_single_t_nw = false; // Toggle between name & work
    int body_standard_varied_vcount = 0; 
    bool is_asymetric = false;

    int buffer_int = -1; // default -1: AKA 'F' or 'no weight/rpe'

    while (current_postition < emlstringlen) {
        char current = emlString[current_postition];

        switch (current) {
        case (int)'\"':
            ++current_postition;
            break;
        case (int)':':
            // Upgrade to asymetric_k
            if (body_single_t_nw == true) {
                switch (kind) {
                case none:
                    tst->no_work = malloc(sizeof(int));
                    break;
                case standard:
                    switch (modifier) {
                    case no_mod:
                        tst->standard_work->reps.value = buffer_int;
                        break;
                    case weight_mod:
                        tst->standard_work->reps.weight = buffer_int;
                        break;
                    case rpe_mod:
                        tst->standard_work->reps.rpe = buffer_int;
                        break;
                    }

                    modifier = no_mod;
                case standard_varied:
                    switch (modifier) {
                    case no_mod:
                        break;
                    case weight_mod:
                        for (int i = 0; i < tst->standard_varied_work->sets; i++) {
                            if (tst->standard_varied_work->vReps[i].weight == -1 && tst->standard_varied_work->vReps[i].rpe == -1) {
                                tst->standard_varied_work->vReps[i].weight = buffer_int; // Override non-set weight
                            }
                        }
                        break;
                    case rpe_mod:
                        for (int i = 0; i < tst->standard_varied_work->sets; i++) {
                            if (tst->standard_varied_work->vReps[i].rpe == -1 && tst->standard_varied_work->vReps[i].weight == -1) {
                                tst->standard_varied_work->vReps[i].rpe = buffer_int; // Override non-set rpe
                            }
                        }
                        break;
                    }
                    break;
                }

                modifier = no_mod;
                buffer_int = -1;

                is_asymetric = true;
                kind = none;

                // Upgrade to asymetric

                tst->asymetric_work = malloc(sizeof(asymetric_k));
                tst->asymetric_work->left_none_k = NULL;
                tst->asymetric_work->left_standard_k = NULL;
                tst->asymetric_work->left_standard_varied_k = NULL;
                tst->asymetric_work->right_none_k = NULL;
                tst->asymetric_work->right_standard_k = NULL;
                tst->asymetric_work->right_standard_varied_k = NULL;

                if (tst->no_work != NULL) {
                    tst->asymetric_work->left_none_k = tst->no_work;
                    tst->no_work = NULL;
                }
                else if (tst->standard_work != NULL) {
                    tst->asymetric_work->left_standard_k = tst->standard_work;
                    tst->standard_work = NULL;
                }
                else if (tst->standard_varied_work != NULL) {
                    tst->asymetric_work->left_standard_varied_k = tst->standard_varied_work;
                    tst->standard_varied_work = NULL;
                }
            }

            body_single_t_nw = true;
            ++current_postition;
            break;
        case (int)'x':
            tst->standard_work = malloc(sizeof(standard_k));
            tst->standard_work->sets = buffer_int;
            tst->standard_work->reps.weight = -1;
            tst->standard_work->reps.rpe = -1;
            tst->standard_work->reps.isTime = false;
            buffer_int = -1;
            ++current_postition;
            break;
        case (int)'(':
            // Upgrade standard_kind to standard_varied_kind
            tst->standard_varied_work = malloc(sizeof(empty_reps) * tst->standard_work->sets + sizeof(int));
            tst->standard_varied_work->sets = tst->standard_work->sets;

            // standard_varied_kind defaults
            for (int i = 0; i < tst->standard_varied_work->sets; i++) {
                tst->standard_varied_work->vReps[i].weight = -1;
                tst->standard_varied_work->vReps[i].rpe = -1;
                tst->standard_varied_work->vReps[i].isTime = false;
            }

            body_standard_varied_vcount = 0;

            free(tst->standard_work);
            tst->standard_work = NULL;

            kind = standard_varied;
            ++current_postition;
            break;
        case (int)',':
            if (body_standard_varied_vcount > tst->standard_varied_work->sets) {
                printf("Too many variable reps\n");
                exit(1);
            }

            switch (modifier) {
            case no_mod:
                tst->standard_varied_work->vReps[body_standard_varied_vcount].value = buffer_int;
                break;
            case weight_mod:
                tst->standard_varied_work->vReps[body_standard_varied_vcount].weight = buffer_int;
                break;
            case rpe_mod:
                tst->standard_varied_work->vReps[body_standard_varied_vcount].rpe = buffer_int;
                break;
            }

            modifier = no_mod;
            buffer_int = -1;
            body_standard_varied_vcount++;
            ++current_postition;
            break;
        case (int)')':
            if (body_standard_varied_vcount > tst->standard_varied_work->sets) {
                printf("Too many variable reps\n");
                exit(1);
            }

            switch (modifier) {
            case no_mod:
                tst->standard_varied_work->vReps[body_standard_varied_vcount].value = buffer_int;
                break;
            case weight_mod:
                tst->standard_varied_work->vReps[body_standard_varied_vcount].weight = buffer_int;
                break;
            case rpe_mod:
                tst->standard_varied_work->vReps[body_standard_varied_vcount].rpe = buffer_int;
                break;
            }

            modifier = no_mod;

            buffer_int = -1;
            body_standard_varied_vcount++;

            if (body_standard_varied_vcount < tst->standard_varied_work->sets) {
                printf("Too few variable reps\n");
                exit(1);
            }
            ++current_postition;
            break;
        case (int)'T':
            switch (kind) {
            case none:
                printf("You cannot add a modifier to none work");
                exit(1);
                break;
            case standard:
                tst->standard_work->reps.isTime = true;
                break;
            case standard_varied:
                if (body_standard_varied_vcount < tst->standard_varied_work->sets) {
                    tst->standard_varied_work->vReps[body_standard_varied_vcount].isTime = true;
                }
                else {
                    printf("You cannot attach time as a macro");
                    exit(1);
                }
                break;
            }
            ++current_postition;
            break;
        case (int)'@':
            switch (kind) {
            case none:
                printf("You cannot add a modifier to none work");
                exit(1);
                break;
            case standard:
                tst->standard_work->reps.value = buffer_int;
                modifier = weight_mod;
                break;
            case standard_varied:
                // check if attaching inner modifier (EX: 3x(5@120,...,...))
                if (body_standard_varied_vcount < tst->standard_varied_work->sets) {
                    tst->standard_varied_work->vReps[body_standard_varied_vcount].value = buffer_int;
                }
                modifier = weight_mod;
                break;
            }

            buffer_int = -1;
            ++current_postition;
            break;
        case (int)'%':
            switch (kind) {
            case none:
                printf("You cannot add a modifier to none work");
                exit(1);
                break;
            case standard:
                tst->standard_work->reps.value = buffer_int;
                modifier = rpe_mod;
                break;
            case standard_varied:
                if (body_standard_varied_vcount < tst->standard_varied_work->sets) {
                    tst->standard_varied_work->vReps[body_standard_varied_vcount].value = buffer_int;
                }
                modifier = rpe_mod;
                break;
            }

            buffer_int = -1;
            ++current_postition;
            break;
        case (int)';':
            switch (kind) {
            case none:
                tst->no_work = malloc(sizeof(int));
                break;
            case standard:
                switch (modifier) {
                case no_mod:
                    tst->standard_work->reps.value = buffer_int;
                    break;
                case weight_mod:
                    tst->standard_work->reps.weight = buffer_int;
                    break;
                case rpe_mod:
                    tst->standard_work->reps.rpe = buffer_int;
                    break;
                }

                // modifier = no_mod;
                break;
            case standard_varied:
                switch (modifier) {
                case no_mod:
                    break;
                case weight_mod:
                    for (int i = 0; i < tst->standard_varied_work->sets; i++) {
                        if (tst->standard_varied_work->vReps[i].weight == -1 && tst->standard_varied_work->vReps[i].rpe == -1) {
                            tst->standard_varied_work->vReps[i].weight = buffer_int; // Override non-set weight
                        }
                    }
                    break;
                case rpe_mod:
                    for (int i = 0; i < tst->standard_varied_work->sets; i++) {
                        if (tst->standard_varied_work->vReps[i].rpe == -1 && tst->standard_varied_work->vReps[i].weight == -1) {
                            tst->standard_varied_work->vReps[i].rpe = buffer_int; // Override non-set rpe
                        }
                    }
                    break;
                }

                // modifier = no_mod;
                break;
            }

            if (is_asymetric == true) {
                if (tst->no_work != NULL) {
                    tst->asymetric_work->right_none_k = tst->no_work;
                    tst->no_work = NULL;
                }
                else if (tst->standard_work != NULL) {
                    tst->asymetric_work->right_standard_k = tst->standard_work;
                    tst->standard_work = NULL;
                }
                else if (tst->standard_varied_work != NULL) {
                    tst->asymetric_work->right_standard_varied_k = tst->standard_varied_work;
                    tst->standard_varied_work = NULL;
                }
            }

            buffer_int = -1;
            print_token(*tst);

            return; // Give control back
        default:
            if (body_single_t_nw == false) {
                strncat(tst->name, &current, 1);
            }
            else {
                if (kind == none) {
                    kind = standard;
                }

                if (current == (int)'F') {
                    buffer_int = -1;
                }
                else {
                    rolling_int(current, &buffer_int);
                }
            }
            ++current_postition;
            break;
        }
    }
}

void validate_header_t(header_t *h) {

    // printf("parameter: %s, value: %s\n", h->parameter, h->value);

    if (strcmp(h->parameter, "version") == 0) {
        strcpy(version, h->value);
    }

    if (strcmp(h->parameter, "weight") == 0) {
        strcpy(weight, h->value);
    }

    *h = empty_header_t;
}

// Creates an integer character by character left to right
void rolling_int(char new_char, int *dest) {
    if (*dest == -1) {
        *dest = 0;
    }

    *dest = (*dest) * 10 + (int)new_char - '0';
    return;
}

void print_token(single_t s) {
    printf("--- Printing single_t ---\n");
    printf("Name: %s\n", s.name);

    if (s.no_work != NULL) {
        printf("No work\n");
    } else if (s.standard_work != NULL) {
        reps reps = s.standard_work->reps;
        printf("Standard work\n");
        if (reps.isTime) {
            printf("%i time sets ", s.standard_work->sets);

            if (reps.value == -1) {
                printf("to failure ");
            } else {
                printf("of %i seconds ", reps.value);
            }
        } else {
            printf("%i sets ", s.standard_work->sets);

            if (reps.value == -1) {
                printf("to failure ");
            } else {
                printf("of %i reps ", reps.value);
            }
        }

        if (reps.weight != -1) {
            printf("with %i %s", reps.weight, weight);
        }
        if (reps.rpe != -1) {
            printf("with RPE of %i", reps.rpe);
        }

        printf("\n");
    } else if (s.standard_varied_work != NULL) {
        int count = s.standard_varied_work->sets;
        printf("Standard varied work\n");
        printf("%i sets\n", count);
        for (int i = 0; i < count; i++) {
            reps reps = s.standard_varied_work->vReps[i];
            printf(" - ");

            if (reps.isTime) {
                printf("time set ");

                if (reps.value == -1) {
                    printf("to failure ");
                } else {
                    printf("of %i seconds ", reps.value);
                }
            } else {
                if (reps.value == -1) {
                    printf("to failure ");
                } else {
                    printf("of %i reps ", reps.value);
                }
            }

            if (reps.weight != -1) {
                printf("with %i %s", reps.weight, weight);
            }
            if (reps.rpe != -1) {
                printf("with RPE of %i", reps.rpe);
            }

            printf("\n");
        }
    } else if (s.asymetric_work != NULL) {
        printf("Asymetric work\n");

        if (s.asymetric_work->left_none_k != NULL) {
            printf("LEFT: No work\n");
        } else if (s.asymetric_work->left_standard_k != NULL) {
            reps reps = s.asymetric_work->left_standard_k->reps;
            printf("LEFT: Standard work ");

            if (reps.isTime) {
                printf("%i time sets ", s.asymetric_work->left_standard_k->sets);
                if (reps.value == -1) {
                    printf("to failure ");
                } else {
                    printf("of %i seconds ", reps.value);
                }
            } else {
                printf("%i sets ", s.asymetric_work->left_standard_k->sets);

                if (reps.value == -1) {
                    printf("to failure ");
                } else {
                    printf("of %i reps ", reps.value);
                }
            }

            if (reps.weight != -1) {
                printf("with %i %s", reps.weight, weight);
            }
            if (reps.rpe != -1) {
                printf("with RPE of %i", reps.rpe);
            }

            printf("\n");
        } else if (s.asymetric_work->left_standard_varied_k != NULL) {
            int count = s.asymetric_work->left_standard_varied_k->sets;
            printf("LEFT: Standard varied work ");
            printf("%i sets\n", count);
            for (int i = 0; i < count; i++) {
                reps reps = s.asymetric_work->left_standard_varied_k->vReps[i];
                printf(" - ");

                if (reps.isTime) {
                    printf("time set ");

                    if (reps.value == -1) {
                        printf("to failure ");
                    } else {
                        printf("of %i seconds ", reps.value);
                    }
                } else {
                    if (reps.value == -1) {
                        printf("to failure ");
                    } else {
                        printf("of %i reps ", reps.value);
                    }
                }

                if (reps.weight != -1) {
                    printf("with %i %s", reps.weight, weight);
                }
                if (reps.rpe != -1) {
                    printf("with RPE of %i", reps.rpe);
                }

                printf("\n");
            }
        }

        if (s.asymetric_work->right_none_k != NULL) {
            printf("RIGHT: No work\n");
        } else if (s.asymetric_work->right_standard_k != NULL) {
            reps reps = s.asymetric_work->right_standard_k->reps;
            printf("RIGHT: Standard work ");

            if (reps.isTime) {
                printf("%i time sets ", s.asymetric_work->right_standard_k->sets);

                if (reps.value == -1) {
                    printf("to failure ");
                } else {
                    printf("of %i seconds ", reps.value);
                }
            } else {
                printf("%i sets ", s.asymetric_work->right_standard_k->sets);

                if (reps.value == -1) {
                    printf("to failure ");
                } else {
                    printf("of %i reps ", reps.value);
                }
            }

            if (reps.weight != -1) {
                printf("with %i %s", reps.weight, weight);
            }
            if (reps.rpe != -1) {
                printf("with RPE of %i", reps.rpe);
            }

            printf("\n");
        } else if (s.asymetric_work->right_standard_varied_k != NULL) {
            int count = s.asymetric_work->right_standard_varied_k->sets;
            printf("RIGHT: Standard varied work ");
            printf("%i sets\n", count);
            for (int i = 0; i < count; i++) {
                reps reps = s.asymetric_work->right_standard_varied_k->vReps[i];
                printf(" - ");

                if (reps.isTime) {
                    printf("time set ");

                    if (reps.value == -1) {
                        printf("to failure ");
                    } else {
                        printf("of %i seconds ", reps.value);
                    }
                } else {
                    if (reps.value == -1) {
                        printf("to failure ");
                    } else {
                        printf("of %i reps ", reps.value);
                    }
                }

                if (reps.weight != -1) {
                    printf("with %i %s", reps.weight, weight);
                }
                if (reps.rpe != -1) {
                    printf("with RPE of %i", reps.rpe);
                }

                printf("\n");
            }
        }
    }
    return;
}

void print_super(super_t s) {
    printf("-------------------- Super --------------------\n");

    for (int i = 0; i < s.count; i++) {
        print_token(s.sets[i]);
    }

    printf("------------------ Super End ------------------\n");

    return;
}