/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "Test.h"
#include "SkRandom.h"
#include "SkRTree.h"
#include "SkTSort.h"

static const size_t RTREE_MIN_CHILDREN = 6;
static const size_t RTREE_MAX_CHILDREN = 11;

static const int NUM_RECTS = 200;
static const size_t NUM_ITERATIONS = 100;
static const size_t NUM_QUERIES = 50;

static const SkScalar MAX_SIZE = 1000.0f;

struct DataRect {
    SkRect rect;
    void* data;
};

static SkRect random_rect(SkRandom& rand) {
    SkRect rect = {0,0,0,0};
    while (rect.isEmpty()) {
        rect.fLeft   = rand.nextRangeF(0, MAX_SIZE);
        rect.fRight  = rand.nextRangeF(0, MAX_SIZE);
        rect.fTop    = rand.nextRangeF(0, MAX_SIZE);
        rect.fBottom = rand.nextRangeF(0, MAX_SIZE);
        rect.sort();
    }
    return rect;
}

static void random_data_rects(SkRandom& rand, DataRect out[], int n) {
    for (int i = 0; i < n; ++i) {
        out[i].rect = random_rect(rand);
        out[i].data = reinterpret_cast<void*>(i);
    }
}

static bool verify_query(SkRect query, DataRect rects[],
                         SkTDArray<void*>& found) {
    // TODO(mtklein): no need to do this after everything's SkRects
    query.roundOut();

    SkTDArray<void*> expected;
    // manually intersect with every rectangle
    for (int i = 0; i < NUM_RECTS; ++i) {
        if (SkRect::Intersects(query, rects[i].rect)) {
            expected.push(rects[i].data);
        }
    }

    if (expected.count() != found.count()) {
        return false;
    }

    if (0 == expected.count()) {
        return true;
    }

    // Just cast to long since sorting by the value of the void*'s was being problematic...
    SkTQSort(reinterpret_cast<long*>(expected.begin()),
             reinterpret_cast<long*>(expected.end() - 1));
    SkTQSort(reinterpret_cast<long*>(found.begin()),
             reinterpret_cast<long*>(found.end() - 1));
    return found == expected;
}

static void run_queries(skiatest::Reporter* reporter, SkRandom& rand, DataRect rects[],
                        SkBBoxHierarchy& tree) {
    for (size_t i = 0; i < NUM_QUERIES; ++i) {
        SkTDArray<void*> hits;
        SkRect query = random_rect(rand);
        tree.search(query, &hits);
        REPORTER_ASSERT(reporter, verify_query(query, rects, hits));
    }
}

static void tree_test_main(SkBBoxHierarchy* tree, int minChildren, int maxChildren,
                           skiatest::Reporter* reporter) {
    DataRect rects[NUM_RECTS];
    SkRandom rand;
    REPORTER_ASSERT(reporter, tree);

    int expectedDepthMin = -1;
    int expectedDepthMax = -1;

    int tmp = NUM_RECTS;
    if (maxChildren > 0) {
        while (tmp > 0) {
            tmp -= static_cast<int>(pow(static_cast<double>(maxChildren),
                                    static_cast<double>(expectedDepthMin + 1)));
            ++expectedDepthMin;
        }
    }

    tmp = NUM_RECTS;
    if (minChildren > 0) {
        while (tmp > 0) {
            tmp -= static_cast<int>(pow(static_cast<double>(minChildren),
                                    static_cast<double>(expectedDepthMax + 1)));
            ++expectedDepthMax;
        }
    }

    for (size_t i = 0; i < NUM_ITERATIONS; ++i) {
        random_data_rects(rand, rects, NUM_RECTS);

        // First try bulk-loaded inserts
        for (int i = 0; i < NUM_RECTS; ++i) {
            tree->insert(rects[i].data, rects[i].rect, true);
        }
        tree->flushDeferredInserts();
        run_queries(reporter, rand, rects, *tree);
        REPORTER_ASSERT(reporter, NUM_RECTS == tree->getCount());
        REPORTER_ASSERT(reporter,
            ((expectedDepthMin <= 0) || (expectedDepthMin <= tree->getDepth())) &&
            ((expectedDepthMax <= 0) || (expectedDepthMax >= tree->getDepth())));
        tree->clear();
        REPORTER_ASSERT(reporter, 0 == tree->getCount());

        // Then try immediate inserts
        tree->insert(rects[0].data, rects[0].rect);
        tree->flushDeferredInserts();
        for (int i = 1; i < NUM_RECTS; ++i) {
            tree->insert(rects[i].data, rects[i].rect);
        }
        run_queries(reporter, rand, rects, *tree);
        REPORTER_ASSERT(reporter, NUM_RECTS == tree->getCount());
        REPORTER_ASSERT(reporter,
            ((expectedDepthMin <= 0) || (expectedDepthMin <= tree->getDepth())) &&
            ((expectedDepthMax <= 0) || (expectedDepthMax >= tree->getDepth())));
        tree->clear();
        REPORTER_ASSERT(reporter, 0 == tree->getCount());

        // And for good measure try immediate inserts, but in reversed order
        tree->insert(rects[NUM_RECTS - 1].data, rects[NUM_RECTS - 1].rect);
        tree->flushDeferredInserts();
        for (int i = NUM_RECTS - 2; i >= 0; --i) {
            tree->insert(rects[i].data, rects[i].rect);
        }
        run_queries(reporter, rand, rects, *tree);
        REPORTER_ASSERT(reporter, NUM_RECTS == tree->getCount());
        REPORTER_ASSERT(reporter,
            ((expectedDepthMin < 0) || (expectedDepthMin <= tree->getDepth())) &&
            ((expectedDepthMax < 0) || (expectedDepthMax >= tree->getDepth())));
        tree->clear();
        REPORTER_ASSERT(reporter, 0 == tree->getCount());
    }
}

DEF_TEST(BBoxHierarchy, reporter) {
    // RTree
    {
        SkRTree* rtree = SkRTree::Create(RTREE_MIN_CHILDREN, RTREE_MAX_CHILDREN);
        SkAutoUnref au(rtree);
        tree_test_main(rtree, RTREE_MIN_CHILDREN, RTREE_MAX_CHILDREN, reporter);

        // Rtree that orders input rectangles on deferred insert.
        SkRTree* unsortedRtree = SkRTree::Create(RTREE_MIN_CHILDREN, RTREE_MAX_CHILDREN, 1, false);
        SkAutoUnref auo(unsortedRtree);
        tree_test_main(unsortedRtree, RTREE_MIN_CHILDREN, RTREE_MAX_CHILDREN, reporter);
    }
}
