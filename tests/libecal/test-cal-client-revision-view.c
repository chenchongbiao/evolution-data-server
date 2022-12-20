/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdlib.h>
#include <libecal/libecal.h>

#include "e-test-server-utils.h"

static ETestServerClosure cal_closure_sync =
	{ E_TEST_SERVER_CALENDAR, NULL, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, FALSE, NULL, FALSE };
static ETestServerClosure cal_closure_async =
	{ E_TEST_SERVER_CALENDAR, NULL, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, FALSE, NULL, TRUE };

typedef enum {
	SUBTEST_OBJECTS_ADDED,
	SUBTEST_OBJECTS_MODIFIED,
	SUBTEST_OBJECTS_REMOVED,
	SUBTEST_VIEW_DONE,
	NUM_SUBTESTS,
	SUBTEST_RESET
} SubTestId;

static void
subtest_passed (SubTestId id,
                GMainLoop *loop)
{
	static guint subtests_complete = 0;

	if (id == SUBTEST_RESET) {
		subtests_complete = 0;
		return;
	}

	subtests_complete |= (1 << id);

	if (subtests_complete == ((1 << NUM_SUBTESTS) - 1))
		g_main_loop_quit (loop);
}

static ICalTime *
get_last_modified (ICalComponent *component)
{
	ICalComponent *inner = i_cal_component_get_inner (component);
	ICalProperty *prop;
	ICalTime *res;

	if (!inner)
		return i_cal_time_new_null_time ();

	prop = i_cal_component_get_first_property (inner, I_CAL_LASTMODIFIED_PROPERTY);

	if (prop) {
		res = i_cal_property_get_lastmodified (prop);
		g_object_unref (prop);
	} else {
		res = i_cal_time_new_null_time ();
	}

	g_clear_object (&inner);

	return res;
}

static void
objects_added_cb (GObject *object,
                  const GSList *objects,
                  gpointer data)
{
	const GSList *l;
	GMainLoop *loop = (GMainLoop *) data;

	for (l = objects; l; l = l->next) {
		ICalComponent *component = l->data;
		ICalTime *recurrence = i_cal_component_get_recurrenceid (component);
		ICalTime *last_modified = get_last_modified (component);
		gchar *str_recurrence, *str_last_modified;

		str_recurrence = i_cal_time_as_ical_string (recurrence);
		str_last_modified = i_cal_time_as_ical_string (last_modified);

		g_print (
			"Object added %s (recurrence id:%s, last-modified:%s)\n",
			i_cal_component_get_uid (component),
			str_recurrence,
			str_last_modified);

		g_clear_object (&recurrence);
		g_clear_object (&last_modified);
		g_free (str_recurrence);
		g_free (str_last_modified);

		g_assert (i_cal_component_get_summary (component) == NULL);
	}

	subtest_passed (SUBTEST_OBJECTS_ADDED, loop);
}

static void
objects_modified_cb (GObject *object,
                     const GSList *objects,
                     gpointer data)
{
	const GSList *l;
	GMainLoop *loop = (GMainLoop *) data;

	for (l = objects; l; l = l->next) {
		ICalComponent *component = l->data;
		ICalTime *recurrence = i_cal_component_get_recurrenceid (component);
		ICalTime *last_modified = get_last_modified (component);
		gchar *str_recurrence, *str_last_modified;

		str_recurrence = i_cal_time_as_ical_string (recurrence);
		str_last_modified = i_cal_time_as_ical_string (last_modified);

		g_print (
			"Object modified %s (recurrence id:%s, last-modified:%s)\n",
			i_cal_component_get_uid (component),
			str_recurrence,
			str_last_modified);

		g_clear_object (&recurrence);
		g_clear_object (&last_modified);
		g_free (str_recurrence);
		g_free (str_last_modified);

		g_assert (i_cal_component_get_summary (component) == NULL);
	}

	subtest_passed (SUBTEST_OBJECTS_MODIFIED, loop);
}

static void
objects_removed_cb (GObject *object,
                    const GSList *objects,
                    gpointer data)
{
	GMainLoop *loop = (GMainLoop *) data;
	const GSList *l;

	for (l = objects; l; l = l->next) {
		ECalComponentId *id = l->data;

		g_print ("Object removed: uid: %s, rid: %s\n", e_cal_component_id_get_uid (id), e_cal_component_id_get_rid (id));
	}

	subtest_passed (SUBTEST_OBJECTS_REMOVED, loop);
}

static void
complete_cb (GObject *object,
             const GError *error,
             gpointer data)
{
	GMainLoop *loop = (GMainLoop *) data;

	g_print ("View complete (status: %d, error_msg:%s)\n", error ? error->code : 0, error ? error->message : "NULL");

	subtest_passed (SUBTEST_VIEW_DONE, loop);
}

static gpointer
alter_cal_client (gpointer user_data)
{
	ECalClient *cal_client = user_data;
	GError *error = NULL;
	ICalComponent *icomp;
	ICalTime *now, *itt;
	gchar *uid = NULL;

	g_return_val_if_fail (cal_client != NULL, NULL);

	now = i_cal_time_new_current_with_zone (i_cal_timezone_get_utc_timezone ());
	itt = i_cal_time_new_from_timet_with_zone (i_cal_time_as_timet (now) + 60 * 60 * 60, 0, NULL);

	icomp = i_cal_component_new (I_CAL_VEVENT_COMPONENT);
	i_cal_component_set_summary (icomp, "Initial event summary");
	i_cal_component_set_dtstart (icomp, now);
	i_cal_component_set_dtend   (icomp, itt);

	if (!e_cal_client_create_object_sync (cal_client, icomp, E_CAL_OPERATION_FLAG_NONE, &uid, NULL, &error))
		g_error ("create object sync: %s", error->message);

	i_cal_component_set_uid (icomp, uid);
	i_cal_component_set_summary (icomp, "Modified event summary");

	if (!e_cal_client_modify_object_sync (cal_client, icomp, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE, NULL, &error))
		g_error ("modify object sync: %s", error->message);

	if (!e_cal_client_remove_object_sync (cal_client, uid, NULL, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE, NULL, &error))
		g_error ("remove object sync: %s", error->message);

	g_object_unref (icomp);
	g_object_unref (now);
	g_object_unref (itt);
	g_free (uid);

	return FALSE;
}

static void
async_get_view_ready (GObject *source_object,
                      GAsyncResult *result,
                      gpointer user_data)
{
	ECalClient *cal_client = E_CAL_CLIENT (source_object);
	ECalClientView *view = NULL;
	GError *error = NULL;
	GMainLoop *loop = (GMainLoop *) user_data;
	GSList *field_list = NULL;

	g_return_if_fail (cal_client != NULL);

	if (!e_cal_client_get_view_finish (cal_client, result, &view, &error))
		g_error ("get view finish: %s", error->message);

	g_object_set_data_full (G_OBJECT (cal_client), "cal-view", view, g_object_unref);

	subtest_passed (SUBTEST_RESET, loop);
	g_signal_connect (view, "objects_added", G_CALLBACK (objects_added_cb), loop);
	g_signal_connect (view, "objects_modified", G_CALLBACK (objects_modified_cb), loop);
	g_signal_connect (view, "objects_removed", G_CALLBACK (objects_removed_cb), loop);
	g_signal_connect (view, "complete", G_CALLBACK (complete_cb), loop);

	field_list = g_slist_prepend (NULL, (gpointer) "UID");
	field_list = g_slist_prepend (field_list, (gpointer) "RECURRENCE-ID");
	field_list = g_slist_prepend (field_list, (gpointer) "LAST-MODIFIED");

	e_cal_client_view_set_fields_of_interest (view, field_list, &error);
	if (error)
		g_error ("set fields of interest: %s", error->message);
	g_slist_free (field_list);

	e_cal_client_view_start (view, NULL);
	alter_cal_client (cal_client);
}

static void
test_get_revision_view_async (ETestServerFixture *fixture,
                              gconstpointer user_data)
{
	ECalClient *cal_client;

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	e_cal_client_get_view (cal_client, "(contains? \"any\" \"event\")", NULL, async_get_view_ready, fixture->loop);
	g_main_loop_run (fixture->loop);

	/* Will unref the view */
	g_object_set_data (G_OBJECT (cal_client), "cal-view", NULL);
}

static void
test_get_revision_view_sync (ETestServerFixture *fixture,
                             gconstpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	ECalClientView *view = NULL;
	GSList *field_list = NULL;

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	if (!e_cal_client_get_view_sync (cal_client, "(contains? \"any\" \"event\")", &view, NULL, &error))
		g_error ("get view sync: %s", error->message);

	subtest_passed (SUBTEST_RESET, fixture->loop);
	g_signal_connect (view, "objects_added", G_CALLBACK (objects_added_cb), fixture->loop);
	g_signal_connect (view, "objects_modified", G_CALLBACK (objects_modified_cb), fixture->loop);
	g_signal_connect (view, "objects_removed", G_CALLBACK (objects_removed_cb), fixture->loop);
	g_signal_connect (view, "complete", G_CALLBACK (complete_cb), fixture->loop);

	field_list = g_slist_prepend (NULL, (gpointer) "UID");
	field_list = g_slist_prepend (field_list, (gpointer) "RECURRENCE-ID");
	field_list = g_slist_prepend (field_list, (gpointer) "LAST-MODIFIED");

	e_cal_client_view_set_fields_of_interest (view, field_list, &error);
	if (error)
		g_error ("set fields of interest: %s", error->message);
	g_slist_free (field_list);

	e_cal_client_view_start (view, NULL);

	g_idle_add ((GSourceFunc) alter_cal_client, cal_client);
	g_main_loop_run (fixture->loop);

	g_object_unref (view);
}

gint
main (gint argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add (
		"/ECalClient/GetRevisionView/Sync",
		ETestServerFixture,
		&cal_closure_sync,
		e_test_server_utils_setup,
		test_get_revision_view_sync,
		e_test_server_utils_teardown);
	g_test_add (
		"/ECalClient/GetRevisionView/Async",
		ETestServerFixture,
		&cal_closure_async,
		e_test_server_utils_setup,
		test_get_revision_view_async,
		e_test_server_utils_teardown);

	return e_test_server_utils_run (argc, argv);
}
