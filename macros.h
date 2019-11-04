#ifndef MACROSES_H_
#define MACROSES_H_

#define bit_set(reg, bit) (reg |= (1<<bit))
#define bit_clear(reg, bit) (reg &= ~(1<<bit))
#define bit_test(reg, bit) (1 & (reg >> (bit)))
#define bit_flip(reg, bit) (reg ^= (1<<bit))

#endif /* MACROSES_H_ */