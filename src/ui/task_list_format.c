#include "ui/task_list_internal.h"

guint
task_list_calculate_cycle_minutes(guint cycles)
{
  const guint focus_minutes = 25;
  const guint short_break_minutes = 5;
  const guint long_break_minutes = 15;
  const guint long_break_interval = 4;

  if (cycles < 1) {
    cycles = 1;
  }

  guint total = cycles * focus_minutes;
  guint breaks = cycles;
  guint long_breaks = breaks / long_break_interval;
  guint short_breaks = breaks - long_breaks;

  total += short_breaks * short_break_minutes;
  total += long_breaks * long_break_minutes;

  return total;
}

char *
task_list_format_minutes(guint minutes)
{
  guint hours = minutes / 60;
  guint mins = minutes % 60;

  if (hours == 0) {
    return g_strdup_printf("%um", mins);
  }

  if (mins == 0) {
    return g_strdup_printf("%uh", hours);
  }

  return g_strdup_printf("%uh %um", hours, mins);
}

char *
task_list_format_cycle_summary(guint cycles)
{
  if (cycles < 1) {
    cycles = 1;
  }

  char *duration = task_list_format_minutes(task_list_calculate_cycle_minutes(cycles));
  char *text = g_strdup_printf("%u cycle%s - %s total",
                               cycles,
                               cycles == 1 ? "" : "s",
                               duration);
  g_free(duration);
  return text;
}
