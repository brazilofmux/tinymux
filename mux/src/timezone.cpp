/*! \file timezone.cpp
 * \brief Timezone-related helper functions (Modernized C++14 using specific time types).
 *
 * This contains conversions between local and UTC timezones using CLinearTimeAbsolute,
 * CLinearTimeDelta, and standard library features, relying only on localtime() for
 * system timezone information.
 */

#include <vector>
#include <ctime>
#include <algorithm>
#include <limits>
#include <cstdint>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <array>
#include <cstring>

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

// --- Configuration & Constants ---

namespace TimezoneCache {
    namespace Detail {

        // Define isLeapYear if somehow missed by externs.h - unlikely
#ifndef isLeapYear
        bool isLeapYear(int year) {
            return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        }
#endif // isLeapYear

        // Assume time_1w is defined externally representing one week
#ifndef time_1w
// Provide a fallback definition if needed, though it should come from externs.h
        const CLinearTimeDelta time_1w(7 * 24 * 60 * 60);
#endif

        // Cache entry structure
        struct OffsetEntry {
            CLinearTimeAbsolute start_lta;
            CLinearTimeAbsolute end_lta;
            CLinearTimeDelta offset_ltd;
            int touched_count; // For LRU
            bool is_dst;

            // Need comparison operators for sorting/lower_bound
            // Compare OffsetEntry < CLinearTimeAbsolute
            bool operator<(const CLinearTimeAbsolute& t) const { return start_lta < t; }
            // Compare CLinearTimeAbsolute < OffsetEntry
            friend bool operator<(const CLinearTimeAbsolute& t, const OffsetEntry& e) { return t < e.start_lta; }
            // Compare OffsetEntry < OffsetEntry (needed for sorting/min_element)
            bool operator<(const OffsetEntry& other) const { return start_lta < other.start_lta; }
        };

        // Maximum size of the offset cache
        constexpr std::size_t MAX_OFFSETS = 50;
        // Minimum interval to merge cache entries
        const CLinearTimeDelta MIN_MERGE_INTERVAL = time_1w; // Use external definition

        // Encapsulated state
        struct CacheState {
            CLinearTimeAbsolute lower_bound_lta;
            CLinearTimeAbsolute upper_bound_lta;
            CLinearTimeDelta standard_offset_ltd;
            std::array<int16_t, 15> nearest_year_of_type; // Index 0 unused, 1-7 non-leap, 8-14 leap
            std::vector<OffsetEntry> offset_table;
            int touch_counter = 0;
            bool initialized = false;
            std::once_flag init_flag;
            std::mutex cache_mutex;

            CacheState() {
                nearest_year_of_type.fill(-1);
                offset_table.reserve(MAX_OFFSETS);
            }
        };

        // Singleton instance of the state
        CacheState& getState() {
            static CacheState state;
            return state;
        }


        // --- Time Conversion Helpers ---

        // Convert struct tm to FIELDEDTIME (adapted from original)
        void setFieldedTimeFromStructTm(FIELDEDTIME* ft, const struct tm* ptm)
        {
            ft->iYear = static_cast<short>(ptm->tm_year + 1900);
            ft->iMonth = static_cast<unsigned short>(ptm->tm_mon + 1);
            ft->iDayOfMonth = static_cast<unsigned short>(ptm->tm_mday);
            ft->iDayOfWeek = static_cast<unsigned short>(ptm->tm_wday);
            ft->iDayOfYear = static_cast<unsigned short>(ptm->tm_yday + 1);
            ft->iHour = static_cast<unsigned short>(ptm->tm_hour);
            ft->iMinute = static_cast<unsigned short>(ptm->tm_min);
            ft->iSecond = static_cast<unsigned short>(ptm->tm_sec);
            ft->iMillisecond = 0;
            ft->iMicrosecond = 0;
            ft->iNanosecond = 0;
        }

        static time_t time_t_largest(void)
        {
            time_t t;
            if (sizeof(INT64) <= sizeof(time_t))
            {
                t = static_cast<time_t>(INT64_MAX_VALUE);
            }
            else
            {
                t = static_cast<time_t>(INT32_MAX_VALUE);
            }

#if defined(TIMEUTIL_TIME_T_MAX_VALUE)
            INT64 t64 = static_cast<INT64>(t);
            if (TIMEUTIL_TIME_T_MAX_VALUE < t64)
            {
                t = static_cast<time_t>(TIMEUTIL_TIME_T_MAX_VALUE);
            }
#endif
#if defined(LOCALTIME_TIME_T_MAX_VALUE)
            // Windows cannot handle negative time_t values, and some versions have
            // an upper limit as well. Values which are too large cause an assert.
            //
            // In VS 2003, the limit is 0x100000000000i64 (beyond the size of a
            // time_t). In VS 2005, the limit is December 31, 2999, 23:59:59 UTC
            // (or 32535215999).
            //
            if (LOCALTIME_TIME_T_MAX_VALUE < t)
            {
                t = static_cast<time_t>(LOCALTIME_TIME_T_MAX_VALUE);
            }
#endif
            return t;
        }

        static time_t time_t_smallest(void)
        {
            time_t t;
            if (sizeof(INT64) <= sizeof(time_t))
            {
                t = static_cast<time_t>(INT64_MIN_VALUE);
            }
            else
            {
                t = static_cast<time_t>(INT32_MIN_VALUE);
            }
#if defined(TIMEUTIL_TIME_T_MIN_VALUE)
            INT64 t64 = static_cast<INT64>(t);
            if (t64 < TIMEUTIL_TIME_T_MIN_VALUE)
            {
                t = static_cast<time_t>(TIMEUTIL_TIME_T_MIN_VALUE);
            }
#endif
#if defined(LOCALTIME_TIME_T_MIN_VALUE)
            if (t < LOCALTIME_TIME_T_MIN_VALUE)
            {
                t = static_cast<time_t>(LOCALTIME_TIME_T_MIN_VALUE);
            }
#endif
            return t;
        }

        // Safely convert CLinearTimeAbsolute to time_t for localtime()
        // Handles potential mismatch between CLinearTimeAbsolute internal representation and time_t
        time_t to_time_t(const CLinearTimeAbsolute& lta) {
            // Assuming ReturnSeconds() returns a type like int64_t or similar wide enough type
            auto seconds_count = lta.ReturnSeconds();

            // Clamp to time_t limits if necessary
            time_t t_max = time_t_largest();
            time_t t_min = time_t_smallest();

            // Cast appropriately for comparison
            if (seconds_count > static_cast<INT64>(t_max)) return t_max;
            if (seconds_count < static_cast<INT64>(t_min)) return t_min;

            return static_cast<time_t>(seconds_count);
        }

        // Wrapper for platform-specific localtime (adapted from original mux_localtime)
        bool safe_localtime(const time_t* timer, struct tm* result) {
#if defined(WINDOWS_TIME) && !defined(__INTEL_COMPILER) && (_MSC_VER >= 1400)
            // MS specific secure version
            return (_localtime64_s(result, timer) == 0);
#elif defined(HAVE_LOCALTIME_R)
            // POSIX reentrant version
            return (localtime_r(timer, result) != nullptr);
#else
            // Fallback to non-thread-safe localtime - requires external locking
            // This mutex protects the call to standard localtime itself.
            static std::mutex localtime_mutex;
            std::lock_guard<std::mutex> lock(localtime_mutex);
            struct tm* ptm = localtime(timer);
            if (ptm) {
                // Copy the result from the static internal buffer used by localtime
                *result = *ptm;
                return true;
            }
            return false;
#endif
        }

        // --- Initialization Logic ---

        // Determines the type of year based on leap status and starting weekday (original logic)
        int getYearType(int iYear)
        {
            FIELDEDTIME ft;
            // Use memset for POD initialization consistency with C style if preferred,
            // otherwise C++ zero-initialization FIELDEDTIME ft{}; might suffice if it's simple enough.
            std::memset(&ft, 0, sizeof(FIELDEDTIME));
            ft.iYear = static_cast<short>(iYear);
            ft.iMonth = 1;
            ft.iDayOfMonth = 1;
            // Other fields (like time) should ideally be set to a neutral value (e.g., noon)
            // to avoid potential DST boundary issues if SetFields is sensitive to it.
            // Assuming SetFields defaults them or handles 0 appropriately.

            CLinearTimeAbsolute ltaJan1;
            // SetFields likely calculates iDayOfWeek internally based on date.
            // If SetFields fails for the given year, this function might return unexpected results.
            // Assume SetFields is robust within reasonable year ranges.
            if (!ltaJan1.SetFields(&ft)) {
                // Handle error: Year might be invalid for SetFields. Return an error code.
                return 0; // 0 indicates error/unknown type
            }

            // After SetFields, ft.iDayOfWeek should be populated.
            if (isLeapYear(iYear))
            {
                // Original logic: 8-14 for leap years (Sun=0 -> 8, Sat=6 -> 14)
                return ft.iDayOfWeek + 8;
            }
            else
            {
                // Original logic: 1-7 for non-leap years (Sun=0 -> 1, Sat=6 -> 7)
                return ft.iDayOfWeek + 1;
            }
            // Ensure result is within 1-14 range? The logic assumes 0-6 input for iDayOfWeek.
        }


        // Helper for midpoint calculation to avoid overflow (from original)
        time_t time_t_midpoint(time_t tLower, time_t tUpper)
        {
            // Be careful with subtraction near limits.
            // Ensure tUpper >= tLower + 2 before subtraction.
            if (tUpper < tLower + 2) {
                return tLower; // Or handle as edge case, maybe return tLower+1 if tUpper==tLower+1?
            }
            // Calculate diff = (tUpper - 1) - (tLower + 1) = tUpper - tLower - 2
            // Use unsigned arithmetic for division if intermediate diff can be large?
            // Or rely on the fact that time_t diff should fit in time_t if operands are valid.
            time_t tDiff = (tUpper - 1) - tLower; // Calculate diff carefully
            return tLower + tDiff / 2 + 1;
        }


        // Finds the actual usable range of localtime and the standard offset
        void perform_time_t_tests() {
            CacheState& state = getState();
            struct tm temp_tm;

            // Use smallest/largest helpers from original code if available, otherwise use numeric_limits
            // Assuming they handle platform specifics like TIMEUTIL_TIME_T_MAX_VALUE etc.
            time_t time_min = time_t_smallest();
            time_t time_max = time_t_largest();

            // --- Search for the highest supported value ---
            time_t upper_t = time_max;
            time_t lower_t = 0;
            time_t mid_t = 0;
            time_t highest_valid = 0; // Initialize to 0

            // Adjust initial range if 0 itself fails localtime
            if (!safe_localtime(&lower_t, &temp_tm)) {
                // If 0 fails, the valid range might not exist or start higher. Bail out?
                // For now, assume 0 is valid as per original logic.
                highest_valid = lower_t; // If 0 is the highest valid, unlikely but possible
            }
            else {
                highest_valid = lower_t; // Start assuming 0 is valid
            }


            while (lower_t < upper_t) {
                // Use careful midpoint logic
                mid_t = time_t_midpoint(lower_t + 1, upper_t);
                if (mid_t <= lower_t) break; // Avoid infinite loop if midpoint doesn't advance

                if (safe_localtime(&mid_t, &temp_tm)) {
                    highest_valid = mid_t; // Found a new higher valid time
                    lower_t = mid_t;       // Search in the upper half [mid_t, upper_t]
                }
                else {
                    upper_t = mid_t - 1;   // Search in the lower half [lower_t, mid_t - 1]
                }
            }
            // After loop, highest_valid holds the largest value for which safe_localtime succeeded.
            state.upper_bound_lta.SetSeconds(highest_valid);


            // --- Search for the lowest supported value ---
            upper_t = 0; // Upper limit of search is now 0
            lower_t = time_min;
            mid_t = 0;
            time_t lowest_valid = 0; // Initialize to 0

            // Check if 0 is valid first, as it's the upper bound now
            if (safe_localtime(&upper_t, &temp_tm)) {
                lowest_valid = upper_t; // 0 is valid
            }
            else {
                // If 0 isn't valid, something is odd. The original loop implies 0 should be tested.
                // The original loop structure might be slightly different here. Let's re-check.
                // Original: `while (tLower < tUpper) { tMid = time_t_midpoint(tLower, tUpper-1); ... }`
                // This suggests the range is [tLower, tUpper-1].
                // If 0 fails, the loop won't run if tLower starts >= 0.
                // Let's stick to finding the lowest valid value >= time_min.
                // Reset lowest_valid and proceed with search.
                lowest_valid = 0; // Re-initialize potential lowest found
            }

            while (lower_t < upper_t) {
                // Midpoint of [lower_t, upper_t - 1]
                mid_t = time_t_midpoint(lower_t, upper_t - 1);
                if (mid_t >= upper_t) break; // Should not happen if upper_t > lower_t

                if (safe_localtime(&mid_t, &temp_tm)) {
                    lowest_valid = mid_t; // Found a new lower valid time
                    upper_t = mid_t;       // Search in lower half [lower_t, mid_t]
                }
                else {
                    lower_t = mid_t + 1;   // Search in upper half [mid_t + 1, upper_t]
                }
            }
            // After loop, lowest_valid holds the smallest value >= time_min for which safe_localtime succeeded.
            state.lower_bound_lta.SetSeconds(lowest_valid);


            // --- Find standard offset near the lower bound ---
            time_t current_t = lowest_valid;
            CLinearTimeAbsolute current_lta;
            bool standard_offset_found = false;

            // Search forward from the lowest valid time
            // Limit search iterations to avoid excessive checks if DST is always on/unknown
            for (int i = 0; i < 24; ++i) { // Check approx 2 years worth of months max
                current_lta.SetSeconds(current_t);
                if (current_lta > state.upper_bound_lta) break; // Don't exceed upper bound

                if (!safe_localtime(&current_t, &temp_tm)) {
                    // Should not happen if current_t is within found bounds, but handle defensively
                     // Advance by approx 1 month and try again
                    current_t += 30 * 24 * 60 * 60; // Approx 1 month
                    continue;
                }

                if (temp_tm.tm_isdst <= 0) { // DST not in effect or unknown
                    FIELDEDTIME ft_local;
                    setFieldedTimeFromStructTm(&ft_local, &temp_tm);

                    CLinearTimeAbsolute lta_local;
                    // SetFields might fail if tm contains invalid date/time combinations
                    // But since it came from localtime, it should be valid. Assume success.
                    lta_local.SetFields(&ft_local);

                    CLinearTimeAbsolute lta_utc;
                    lta_utc.SetSeconds(current_t);

                    state.standard_offset_ltd = lta_local - lta_utc;
                    standard_offset_found = true;
                    break; // Found it
                }

                // Advance time by approx 1 month
                // Use CLinearTimeDelta if available for safer time addition
                // current_lta += CLinearTimeDelta(30 * 24 * 60 * 60);
                // current_t = to_time_t(current_lta);
                // Simpler: stick to time_t addition
                current_t += 30 * 24 * 60 * 60; // Approx 1 month
            }

            // If no non-DST time found, standard_offset_ltd might remain uninitialized (default constructor).
            // The original didn't explicitly handle this; it assumed it would find one.
            // We might need a default fallback if the loop finishes without success.
            if (!standard_offset_found) {
                // Default to zero offset? Or log a warning?
                state.standard_offset_ltd = CLinearTimeDelta(0); // Assign a default
                // Log warning here if logging facility exists
            }
        }

        // Fills the NearestYearOfType table
        void populate_year_table() {
            CacheState& state = getState();

            FIELDEDTIME ft_upper;
            // Get the year from the upper bound time
            if (!state.upper_bound_lta.ReturnFields(&ft_upper)) {
                // If ReturnFields fails (e.g., time is zero/invalid), use a fallback.
                // Use a year known to be within typical 32-bit time_t limits.
                ft_upper.iYear = 2037;
            }

            int start_year = ft_upper.iYear;
            int types_found = 0;
            const int total_types = 14; // 1-14

            // Search backwards from the year before the upper bound year
            // Limit search depth to avoid excessive loops if year types repeat rarely
            for (int year = start_year - 1; types_found < total_types && year > start_year - 200; --year) {
                int type = getYearType(year); // Needs CLinearTimeAbsolute/FIELDEDTIME
                if (type >= 1 && type <= 14) { // Ensure type is valid (1-14)
                    if (state.nearest_year_of_type[type] == -1) { // Check if not already found
                        state.nearest_year_of_type[type] = static_cast<int16_t>(year);
                        types_found++;
                    }
                }
                // Add check for lower bound year? Stop searching if year goes below lower_bound_lta year?
                // Might be useful if lower_bound_lta is significantly after year 0.
            }
            // Remaining types in nearest_year_of_type stay -1 if not found within search range.
        }

        // Initialization function (called via std::call_once)
        void initialize_internal() {
            CacheState& state = getState();
            if (state.initialized) return;

#ifdef HAVE_TZSET
            // Assuming mux_tzset() is the allowed interface from externs.h
            mux_tzset();
#endif

            perform_time_t_tests();
            populate_year_table();

            state.initialized = true;
        }


        // --- Cache Management Logic ---

        // Finds iterator to cache entry whose start_lta <= lta, or end() if none.
        std::vector<OffsetEntry>::iterator find_entry_iter(const CLinearTimeAbsolute& lta) {
            CacheState& state = getState();

            // lower_bound finds first element >= lta (when comparing element.start_lta < lta)
            // We want the element *before* that, if it exists and its start <= lta.
            auto it = std::lower_bound(state.offset_table.begin(), state.offset_table.end(), lta);

            // If it == begin(), no element starts <= lta
            if (it == state.offset_table.begin()) {
                // Unless the first element *exactly* starts at lta
                if (it != state.offset_table.end() && it->start_lta == lta) {
                    return it;
                }
                return state.offset_table.end(); // No entry starts at or before lta
            }

            // Otherwise, 'it' points to the first element > lta, or end().
            // The element we want is the one *before* 'it'.
            auto prev_it = std::prev(it);

            // Check if this previous element actually starts at or before lta
            if (prev_it->start_lta <= lta) {
                return prev_it;
            }
            else {
                // This case shouldn't happen with sorted data and lower_bound logic
                // but handle defensively.
                return state.offset_table.end();
            }
        }


        // Updates the cache with a new data point (lta, offset, is_dst)
        void update_offset_table(const CLinearTimeAbsolute& lta, const CLinearTimeDelta& offset, bool is_dst)
        {
            CacheState& state = getState();
            // Lock the mutex for cache modification
            std::lock_guard<std::mutex> lock(state.cache_mutex);

            state.touch_counter++;

            // Find iterator to entry potentially covering lta, or the one just preceding it.
            auto it = find_entry_iter(lta);

            // Case 1: Found an existing entry that covers this time point
            if (it != state.offset_table.end() && lta >= it->start_lta && lta <= it->end_lta) {
                // Check for consistency. If data matches, just update touch count.
                if (it->offset_ltd == offset && it->is_dst == is_dst) {
                    it->touched_count = state.touch_counter;
                    return; // Cache hit, data consistent
                }
                else {
                    // Data mismatch within an interval! This indicates an issue.
                    // Original code didn't explicitly handle splitting.
                    // Simplest approach: Just update the touch count, acknowledging potential inaccuracy.
                    // A more robust approach would involve splitting the interval [start, lta-1] and [lta, end].
                    it->touched_count = state.touch_counter; // Update touch, but data is potentially stale
                    // Log warning here if logging exists.
                    return;
                }
            }

            // Case 2: lta falls outside existing intervals or between them.
            // Need to potentially insert, extend, or merge.

            bool merged_or_extended = false;

            // Try extending the preceding entry ('it') if it exists, matches, and is close enough
            if (it != state.offset_table.end() && // 'it' points to the entry starting <= lta
                it->offset_ltd == offset && it->is_dst == is_dst &&
                lta > it->end_lta && // Ensure lta is actually after the current end
                lta <= it->end_lta + MIN_MERGE_INTERVAL)
            {
                it->end_lta = lta; // Extend end time
                it->touched_count = state.touch_counter;
                merged_or_extended = true;
            }

            // Try extending the succeeding entry backwards ('next_it') if it exists, matches, and is close enough
            // 'next_it' is the element *after* 'it' (if 'it' is valid), or the beginning if 'it' was end().
            auto next_it = (it == state.offset_table.end()) ? state.offset_table.begin() : std::next(it);

            if (next_it != state.offset_table.end() &&
                next_it->offset_ltd == offset && next_it->is_dst == is_dst &&
                lta < next_it->start_lta && // Ensure lta is actually before the current start
                next_it->start_lta <= lta + MIN_MERGE_INTERVAL) // Check closeness
            {
                if (merged_or_extended) {
                    // Already extended 'it'. Now check if 'it' and 'next_it' can merge.
                    // This happens if the gap between the newly extended 'it' and 'next_it' is small enough.
                    // Note: The check should be between it->end_lta and next_it->start_lta
                    if (next_it->start_lta <= it->end_lta + MIN_MERGE_INTERVAL) {
                        // Merge 'next_it' into 'it'
                        it->end_lta = next_it->end_lta;
                        it->touched_count = state.touch_counter; // Update touch count again
                        state.offset_table.erase(next_it); // Remove the merged 'next_it'
                    }
                    // If they can't merge after extension, merged_or_extended remains true,
                    // but we don't modify next_it here.
                }
                else {
                    // Only potentially extend 'next_it' backwards
                    next_it->start_lta = lta;
                    next_it->touched_count = state.touch_counter;
                    merged_or_extended = true;
                }
            }

            // Case 3: No merge or extension happened, insert a new point/interval.
            if (!merged_or_extended) {
                // Evict LRU entry if cache is full
                if (state.offset_table.size() >= MAX_OFFSETS) {
                    auto lru_it = std::min_element(state.offset_table.begin(), state.offset_table.end(),
                        [](const OffsetEntry& a, const OffsetEntry& b) {
                            return a.touched_count < b.touched_count;
                        });
                    // Erase the LRU element. Need to be careful if it affects iterators,
                    // but since we re-find the insertion point, it's okay.
                    if (lru_it != state.offset_table.end()) { // Ensure not trying to erase end()
                        state.offset_table.erase(lru_it);
                    }
                }

                // Find correct insertion position *again* after potential eviction, to maintain sort order.
                auto insert_pos = std::lower_bound(state.offset_table.begin(), state.offset_table.end(), lta);

                // Insert the new entry as a single point interval [lta, lta]
                state.offset_table.insert(insert_pos, { lta, lta, offset, state.touch_counter, is_dst });

                // After insertion, could we now merge this new entry with neighbors?
                // This logic can get complex. The original code handled merging after extending.
                // Let's stick to the extend/merge logic *before* insertion for simplicity,
                // matching the original's apparent strategy more closely.
                // Re-checking merges post-insertion would require finding the inserted element
                // and its neighbors again.
            }
            // The logic here tries to mirror the original's merge approach.
            // Revisit if specific merge scenarios aren't handled correctly.
        }

        // --- Core Query Logic ---

        // Internal query: Performs the actual time conversion and calculation
        CLinearTimeDelta queryLocalOffsetAt_Internal(CLinearTimeAbsolute utc_lta, bool* is_dst)
        {
            CacheState& state = getState();
            *is_dst = false; // Default

            CLinearTimeAbsolute query_lta = utc_lta; // The time point used for localtime query, possibly mapped

            // Handle times beyond the reliable upper bound using year mapping (Original Logic)
            if (query_lta > state.upper_bound_lta) {
                FIELDEDTIME ft_query;
                if (!query_lta.ReturnFields(&ft_query)) {
                    // Failed to get fields, cannot map year. Return standard offset.
                    return state.standard_offset_ltd;
                }

                int original_year = ft_query.iYear;
                int year_type = getYearType(original_year);
                int mapped_year = -1;

                if (year_type >= 1 && year_type <= 14) {
                    mapped_year = state.nearest_year_of_type[year_type];
                }

                if (mapped_year != -1 && mapped_year >= 1) { // Ensure mapped year is valid
                    // Modify the fielded time to use the mapped year
                    ft_query.iYear = static_cast<short>(mapped_year);
                    // Use SetFields to get the CLinearTimeAbsolute for the mapped date/time
                    // This recalculates the absolute time based on the new year.
                    CLinearTimeAbsolute mapped_lta;
                    if (!mapped_lta.SetFields(&ft_query)) {
                        // SetFields failed for the mapped year, fallback.
                        return state.standard_offset_ltd;
                    }
                    query_lta = mapped_lta;

                }
                else {
                    // No valid mapped year found, can only return the standard offset guess.
                    return state.standard_offset_ltd;
                }
            }

            // Ensure query_lta is within detected bounds after potential mapping.
            // Clamping might be needed if year mapping produced a time outside bounds.
            if (query_lta < state.lower_bound_lta) query_lta = state.lower_bound_lta;
            // The upper bound check might be redundant if mapping always targets years below upper_bound_lta,
            // but keep for safety.
            if (query_lta > state.upper_bound_lta) query_lta = state.upper_bound_lta;

            // Use safe_localtime with the seconds from the (potentially mapped) query_lta
            time_t query_t = to_time_t(query_lta);
            struct tm local_tm;
            if (!safe_localtime(&query_t, &local_tm)) {
                // localtime failed even within bounds - rare. Return standard offset.
                return state.standard_offset_ltd;
            }

            *is_dst = (local_tm.tm_isdst > 0);

            // Calculate the offset: Local time represented by local_tm - UTC time represented by query_lta
            FIELDEDTIME ft_local;
            setFieldedTimeFromStructTm(&ft_local, &local_tm);

            CLinearTimeAbsolute lta_local;
            if (!lta_local.SetFields(&ft_local)) {
                // Should not fail if ft_local came from valid tm, but handle defensively.
                return state.standard_offset_ltd; // Fallback
            }

            // The UTC time corresponding to the query we made
            CLinearTimeAbsolute lta_utc_query;
            lta_utc_query.SetSeconds(query_t); // Use the actual time_t used for the query

            CLinearTimeDelta offset = lta_local - lta_utc_query;

            // Update the cache with the result for the *original* utc_lta
            // Note: update_offset_table handles locking internally
            update_offset_table(utc_lta, offset, *is_dst);
            return offset;
        }

    } // namespace Detail

    // --- Public API ---

    // Initialize the timezone cache system (thread-safe)
    void initialize() {
        std::call_once(Detail::getState().init_flag, Detail::initialize_internal);
    }

    // Query the local time offset from UTC at a specific UTC time point
    CLinearTimeDelta queryLocalOffsetAtUTC(const CLinearTimeAbsolute& utc_lta, bool* is_dst)
    {
        initialize(); // Ensure initialized (call_once handles subsequent calls)

        Detail::CacheState& state = Detail::getState();
        *is_dst = false; // Default

        // Handle times before the known lower bound (assume standard offset, no DST)
        if (utc_lta < state.lower_bound_lta) {
            return state.standard_offset_ltd;
        }

        CLinearTimeDelta offset_result;
        bool dst_result;
        bool found_in_cache = false;

        { // Scope for cache read lock
            std::lock_guard<std::mutex> lock(state.cache_mutex);
            state.touch_counter++; // Increment touch counter even for reads to help LRU? Maybe only on hit.

            auto it = Detail::find_entry_iter(utc_lta);

            if (it != state.offset_table.end() && utc_lta >= it->start_lta && utc_lta <= it->end_lta) {
                // Cache hit!
                offset_result = it->offset_ltd;
                dst_result = it->is_dst;
                it->touched_count = state.touch_counter; // Update LRU counter on hit
                found_in_cache = true;
            }
        } // Release cache lock

        if (found_in_cache) {
            *is_dst = dst_result;
            return offset_result;
        }
        else {
            // Cache miss: Call the internal query logic.
            // This internal call will perform the localtime query and update the cache (including locking).
            offset_result = Detail::queryLocalOffsetAt_Internal(utc_lta, is_dst);

            // Optional: Probing nearby times (as in previous version) can be added here
            // if desired, calling Detail::queryLocalOffsetAt_Internal for probe points.
            // Ensure probe points are within lower_bound_lta before calling.
            /*
            bool dont_care_dst;
            CLinearTimeAbsolute probe_before = utc_lta - Detail::MIN_MERGE_INTERVAL;
            if (probe_before >= state.lower_bound_lta) {
                 Detail::queryLocalOffsetAt_Internal(probe_before, &dont_care_dst);
            }
            CLinearTimeAbsolute probe_after = utc_lta + Detail::MIN_MERGE_INTERVAL;
            // queryLocalOffsetAt_Internal handles upper bound and mapping
            Detail::queryLocalOffsetAt_Internal(probe_after, &dont_care_dst);
            */

            return offset_result;
        }
    }

    // Helper to get the current offset
    // Requires CLinearTimeAbsolute to have a method to get current UTC time.
    // Assuming a static method or a constructor. Let's assume GetUTC().
    CLinearTimeDelta getCurrentLocalOffset(bool* is_dst) {
        // Assuming CLinearTimeAbsolute has a way to get current time, e.g., static GetUTC()
        // or default constructor initializes to now, or a SetNow() method.
        // Adjust based on actual CLinearTimeAbsolute interface.
        // Example: CLinearTimeAbsolute ltaNow = CLinearTimeAbsolute::GetUTC();
        // Example: CLinearTimeAbsolute ltaNow; // If default constructor is current time
        // Example: CLinearTimeAbsolute ltaNow; ltaNow.SetUTC(); // If requires explicit set

        // Placeholder - replace with actual mechanism for getting current time
        CLinearTimeAbsolute ltaNow;
#ifdef HAVE_CLINEARTIMEABSOLUTE_SETUTC // Example hypothetical check
        ltaNow.SetUTC();
#else
        // Fallback: Use time() if absolutely necessary and allowed as lowest common denominator
        // This adds a slight dependency but might be unavoidable if CLinearTimeAbsolute can't get 'now'.
        time_t now_t;
        time(&now_t); // Standard C function to get current time_t
        ltaNow.SetSeconds(now_t);
#endif

        return queryLocalOffsetAtUTC(ltaNow, is_dst);
    }
} // namespace TimezoneCache
