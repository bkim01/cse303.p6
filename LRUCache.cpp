#include <iostream>
#include <stdio.h>
#include <string.h>

typedef struct {
    char key[512];
    char val[512];
} elem;

class LRUCache{
public:
    elem *arr;
    int sz;  // total number of elements in the list currently.
    int cap;

    LRUCache(int capacity) {
        arr = new elem[capacity];
        sz = 0;
        cap = capacity;
    }

    /* move the used element to the end of list */
    void adjust(int a) {
        if (a == sz - 1) {
            return ;
        }
        elem cur = arr[a];
        for (int i = a; i < sz - 1; i ++) {
            arr[i] = arr[i + 1]; // move others 1 pos left
        }
        arr[sz - 1] = cur; // move to the end
    }

    char * get(char * key) {
        for (int i = 0; i < sz; i ++) {
            if (strcmp(arr[i].key, key) == 0) {
                char * a = (char *)malloc(512 * sizeof(char));
                strcpy(a, arr[i].val);
                adjust(i);
                return a; // existent key
            }
        }
        return NULL;
    }

    void put(char * key, char * value) {
        for (int i = 0; i < sz; i ++) {
            if (strcmp(arr[i].key, key) == 0) { // existent
                strcpy(arr[i].val, value);
                adjust(i);
                return;
            }
        }
        if (sz == cap) { // check if reach the capacity
            for (int i = 0; i < sz - 1; i ++) {
                arr[i] = arr[i + 1]; // delete the least used element
            }
            strcpy(arr[sz - 1].key, key);
            strcpy(arr[sz - 1].val, value);
        } else {
            strcpy(arr[sz].key, key);
            strcpy(arr[sz].val, value);
            sz ++; // increase the size
        }
    }
};
