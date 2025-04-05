#include "tests/test.h"

int main(void) {
    feature_test_build();

    slice_t *args = slice_new();
    slice_push(args, "stories15M.bin");
    slice_push(args, "-s");
    slice_push(args, "1743659096");
    slice_push(args, "-i");
    slice_push(args, "hello");
    //    exec_imm(WORKDIR, BUILD_OUTPUT, args);

    char *actual = exec_with_args(args);
    char *expect = "hello, God had made everything better. \n"
                   "The people around him stopped to talk to each other. Suddenly, a loud noise scared them all away. It was an angry voice from the tower. \n"
                   "The people around were angry with the people too and kept saying, \"Run away!\" \n"
                   "The people ran back the way they came, but they soon realized it was not good to be so angry about someone again. It was just a mischievous little child. \n"
                   "The people felt very sad that their side was in trouble. They decided to use sign language to get the sound. They said some words and waved their hands and laughed loudly. \n"
                   "The broken pieces of disagreement came apart and the people stopped being angry. But they still remembered that the people were so proud of their prayer and the little child who had been so scared, they would never forget it!\n"
                   "achieved tok/s:";
    assert_true(strstr(actual, expect));
    printf("%s", actual);
}
