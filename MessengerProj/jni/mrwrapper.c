/*******************************************************************************
 *
 *                              Delta Chat Android
 *                           (C) 2017 Björn Petersen
 *                    Contact: r10s@b44t.com, http://b44t.com
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see http://www.gnu.org/licenses/ .
 *
 *******************************************************************************
 *
 * File:    mrwrapper.c
 * Purpose: The C part of the Java<->C Wrapper, see also MrMailbox.java
 *
 ******************************************************************************/


#include <jni.h>
#include <android/log.h>
#include "messenger-backend/src/deltachat.h"
#include "messenger-backend/cmdline/cmdline.h"


#define CHAR_REF(a) \
	const char* a##Ptr = (a)? (*env)->GetStringUTFChars(env, (a), 0) : NULL; // passing a NULL-jstring results in a NULL-ptr - this is needed by functions using eg. NULL for "delete"

#define CHAR_UNREF(a) \
	if(a) { (*env)->ReleaseStringUTFChars(env, (a), a##Ptr); }

#define JSTRING_NEW(a) jstring_new__(env, (a))
static jstring jstring_new__(JNIEnv* env, const char* a)
{
	if( a==NULL || a[0]==0 ) {
		return (*env)->NewStringUTF(env, "");
	}

	/* for non-empty strings, do not use NewStringUTF() as this is buggy on some Android versions.
	Instead, create the string using `new String(ByteArray, "UTF-8);` which seems to be programmed more properly.
	(eg. on KitKat a simple "SMILING FACE WITH SMILING EYES" (U+1F60A, UTF-8 F0 9F 98 8A) will let the app crash, reporting 0xF0 is a bad UTF-8 start,
	see http://stackoverflow.com/questions/12127817/android-ics-4-0-ndk-newstringutf-is-crashing-down-the-app ) */
	static jclass    s_strCls    = NULL;
	static jmethodID s_strCtor   = NULL;
	static jclass    s_strEncode = NULL;
	if( s_strCls==NULL ) {
		s_strCls    = (*env)->NewGlobalRef(env, (*env)->FindClass(env, "java/lang/String"));
		s_strCtor   = (*env)->GetMethodID(env, s_strCls, "<init>", "([BLjava/lang/String;)V");
		s_strEncode = (*env)->NewGlobalRef(env, (*env)->NewStringUTF(env, "UTF-8"));
	}

	int a_bytes = strlen(a);
	jbyteArray array = (*env)->NewByteArray(env, a_bytes);
		(*env)->SetByteArrayRegion(env, array, 0, a_bytes, a);
		jstring ret = (jstring) (*env)->NewObject(env, s_strCls, s_strCtor, array, s_strEncode);
	(*env)->DeleteLocalRef(env, array); /* we have to delete the reference as it is not returned to Java, AFAIK */

	return ret;
}


/* global stuff */

static JavaVM*   s_jvm = NULL;
static jclass    s_MrMailbox_class = NULL;
static jmethodID s_MrCallback_methodID = NULL;
static int       s_global_init_done = 0;


static void s_init_globals(JNIEnv *env, jclass MrMailbox_class)
{
	/* make sure, the intialisation is done only once */
	if( s_global_init_done ) { return; }
	s_global_init_done = 1;

	/* prepare calling back a Java function */
	__android_log_print(ANDROID_LOG_INFO, "DeltaChat", "JNI: s_init_globals()..."); /* low-level logging, dc_log_*() may not be yet available. However, please note that __android_log_print() may not work (eg. on LG X Cam) */

	(*env)->GetJavaVM(env, &s_jvm); /* JNIEnv cannot be shared between threads, so we share the JavaVM object */
	s_MrMailbox_class =  (*env)->NewGlobalRef(env, MrMailbox_class);
	s_MrCallback_methodID = (*env)->GetStaticMethodID(env, MrMailbox_class, "MrCallback","(IJJ)J" /*signature as "(param)ret" with I=int, J=long*/ );
}


/* tools */

static jintArray dc_array2jintArray_n_unref(JNIEnv *env, dc_array_t* ca)
{
	/* takes a C-array of type dc_array_t and converts it it a Java-Array.
	then the C-array is freed and the Java-Array is returned. */
	int i, icnt = ca? dc_array_get_cnt(ca) : 0;
	jintArray ret = (*env)->NewIntArray(env, icnt); if (ret == NULL) { return NULL; }

	if( ca ) {
		if( icnt ) {
			uintptr_t* ca_data = dc_array_get_raw(ca);
			if( sizeof(uintptr_t)==sizeof(jint) ) {
				(*env)->SetIntArrayRegion(env, ret, 0, icnt, (jint*)ca_data);
			}
			else {
				jint* temp = calloc(icnt, sizeof(jint));
					for( i = 0; i < icnt; i++ ) {
						temp[i] = (jint)ca_data[i];
					}
					(*env)->SetIntArrayRegion(env, ret, 0, icnt, temp);
				free(temp);
			}
		}
		dc_array_unref(ca);
	}

	return ret;
}


static uint32_t* jintArray2uint32Pointer(JNIEnv* env, jintArray ja, int* ret_icnt)
{
	/* takes a Java-Array and converts it to a C-Array. */
	uint32_t* ret = NULL;
	if( ret_icnt ) { *ret_icnt = 0; }

	if( env && ja && ret_icnt )
	{
		int i, icnt  = (*env)->GetArrayLength(env, ja);
		if( icnt > 0 )
		{
			const jint* temp = (*env)->GetIntArrayElements(env, ja, NULL);
			if( temp )
			{
				ret = calloc(icnt, sizeof(uint32_t));
				if( ret )
				{
					for( i = 0; i < icnt; i++ ) {
						ret[i] = (uint32_t)temp[i];
					}
					*ret_icnt = icnt;
				}
				(*env)->ReleaseIntArrayElements(env, ja, temp, 0);
			}
		}
	}

	return ret;
}


/*******************************************************************************
 * MrMailbox
 ******************************************************************************/


static dc_context_t* get_dc_context(JNIEnv *env, jclass cls)
{
	static jfieldID fid = 0;
	if( fid == 0 ) {
		fid = (*env)->GetStaticFieldID(env, cls, "m_hMailbox", "J" /*Signature, J=long*/);
	}
	if( fid ) {
		return (dc_context_t*)(*env)->GetStaticLongField(env, cls, fid);
	}
	return NULL;
}


/* MrMailbox - new/delete */

static uintptr_t s_context_callback_(dc_context_t* context, int event, uintptr_t data1, uintptr_t data2)
{
	jlong   l;
	JNIEnv* env;

	#if 0 /* -- __android_log_print() does not log eg. on LG X Cam - but Javas Log.i() etc. do. So, we do not optimize these calls and just use the Java logging. */
	if( event==DC_EVENT_INFO || event==DC_EVENT_WARNING ) {
	    __android_log_print(event==DC_EVENT_INFO? ANDROID_LOG_INFO : ANDROID_LOG_WARN, "DeltaChat", "%s", (char*)data2); /* on problems, add `-llog` to `Android.mk` */
		return 0; /* speed up things for info/warning */
	}
	else if( event == DC_EVENT_ERROR ) {
	    __android_log_print(ANDROID_LOG_ERROR, "DeltaChat", "%s", (char*)data2);
	    /* errors are also forwarded to Java to show them in a bubble or so */
	}
	#endif

	if( s_jvm==NULL || s_MrMailbox_class==NULL || s_MrCallback_methodID==NULL ) {
		return 0; /* may happen on startup */
	}

	(*s_jvm)->GetEnv(s_jvm, &env, JNI_VERSION_1_6); /* as this function may be called from _any_ thread, we cannot use a static pointer to JNIEnv */
	if( env==NULL ) {
		return 0; /* may happen on startup */
	}

	l = (*env)->CallStaticLongMethod(env, s_MrMailbox_class, s_MrCallback_methodID, (jint)event, (jlong)data1, (jlong)data2);
	return (uintptr_t)l;
}


JNIEXPORT jlong Java_com_b44t_messenger_MrMailbox_MrMailboxNew(JNIEnv *env, jclass c)
{
	s_init_globals(env, c);
	dc_no_compound_msgs();
	return (jlong)dc_context_new(s_context_callback_, NULL, "Android");
}


/* MrMailbox - open/configure/connect/fetch */

JNIEXPORT jint Java_com_b44t_messenger_MrMailbox_open(JNIEnv *env, jclass cls, jstring dbfile)
{
	CHAR_REF(dbfile);
		jint ret = dc_open(get_dc_context(env, cls), dbfilePtr, NULL);
	CHAR_UNREF(dbfile)
	return ret;
}


JNIEXPORT void Java_com_b44t_messenger_MrMailbox_close(JNIEnv *env, jclass cls)
{
	dc_close(get_dc_context(env, cls));
}


JNIEXPORT jstring Java_com_b44t_messenger_MrMailbox_getBlobdir(JNIEnv *env, jclass cls)
{
	char* temp = dc_get_blobdir(get_dc_context(env, cls));
		jstring ret = JSTRING_NEW(temp);
	free(temp);
	return ret;
}


JNIEXPORT void Java_com_b44t_messenger_MrMailbox_configure(JNIEnv *env, jclass cls)
{
	dc_configure(get_dc_context(env, cls));
}


JNIEXPORT void Java_com_b44t_messenger_MrMailbox_stopOngoingProcess(JNIEnv *env, jclass cls)
{
	dc_stop_ongoing_process(get_dc_context(env, cls));
}


JNIEXPORT jint Java_com_b44t_messenger_MrMailbox_isConfigured(JNIEnv *env, jclass cls)
{
	return (jint)dc_is_configured(get_dc_context(env, cls));
}


JNIEXPORT void Java_com_b44t_messenger_MrMailbox_performJobs(JNIEnv *env, jclass cls)
{
	dc_perform_imap_jobs(get_dc_context(env, cls));
}


JNIEXPORT void Java_com_b44t_messenger_MrMailbox_idle(JNIEnv *env, jclass cls)
{
	dc_perform_imap_idle(get_dc_context(env, cls));
}


JNIEXPORT void Java_com_b44t_messenger_MrMailbox_interruptIdle(JNIEnv *env, jclass cls)
{
	dc_interrupt_imap_idle(get_dc_context(env, cls));
}


JNIEXPORT void Java_com_b44t_messenger_MrMailbox_fetch(JNIEnv *env, jclass cls)
{
	dc_perform_imap_fetch(get_dc_context(env, cls));
}


JNIEXPORT void Java_com_b44t_messenger_MrMailbox_performSmtpJobs(JNIEnv *env, jclass cls)
{
	dc_perform_smtp_jobs(get_dc_context(env, cls));
}


JNIEXPORT void Java_com_b44t_messenger_MrMailbox_performSmtpIdle(JNIEnv *env, jclass cls)
{
	dc_perform_smtp_idle(get_dc_context(env, cls));
}


JNIEXPORT void Java_com_b44t_messenger_MrMailbox_interruptSmtpIdle(JNIEnv *env, jclass cls)
{
	dc_interrupt_smtp_idle(get_dc_context(env, cls));
}


/* MrMailbox - handle contacts */

JNIEXPORT jintArray Java_com_b44t_messenger_MrMailbox_getContacts(JNIEnv *env, jclass cls, jint flags, jstring query)
{
	CHAR_REF(query);
	    dc_array_t* ca = dc_get_contacts(get_dc_context(env, cls), flags, queryPtr);
	CHAR_UNREF(query);
	return dc_array2jintArray_n_unref(env, ca);
}


JNIEXPORT jint Java_com_b44t_messenger_MrMailbox_getBlockedCount(JNIEnv *env, jclass cls)
{
	return dc_get_blocked_cnt(get_dc_context(env, cls));
}


JNIEXPORT jintArray Java_com_b44t_messenger_MrMailbox_getBlockedContacts(JNIEnv *env, jclass cls)
{
	dc_array_t* ca = dc_get_blocked_contacts(get_dc_context(env, cls));
	return dc_array2jintArray_n_unref(env, ca);
}


JNIEXPORT jlong Java_com_b44t_messenger_MrMailbox_MrMailboxGetContact(JNIEnv *env, jclass c, jlong hMailbox, jint contact_id)
{
	return (jlong)dc_get_contact((dc_context_t*)hMailbox, contact_id);
}


JNIEXPORT jint Java_com_b44t_messenger_MrMailbox_MrMailboxCreateContact(JNIEnv *env, jclass c, jlong hMailbox, jstring name, jstring addr)
{
	CHAR_REF(name);
	CHAR_REF(addr);
		jint ret = (jint)dc_create_contact((dc_context_t*)hMailbox, namePtr, addrPtr);
	CHAR_UNREF(addr);
	CHAR_UNREF(name);
	return ret;
}


JNIEXPORT void Java_com_b44t_messenger_MrMailbox_MrMailboxBlockContact(JNIEnv *env, jclass c, jlong hMailbox, jint contact_id, jint block)
{
	dc_block_contact((dc_context_t*)hMailbox, contact_id, block);
}


JNIEXPORT jint Java_com_b44t_messenger_MrMailbox_MrMailboxDeleteContact(JNIEnv *env, jclass c, jlong hMailbox, jint contact_id)
{
	return (jint)dc_delete_contact((dc_context_t*)hMailbox, contact_id);
}


/* MrMailbox - handle chats */

JNIEXPORT jlong Java_com_b44t_messenger_MrMailbox_MrMailboxGetChatlist(JNIEnv *env, jclass c, jlong hMailbox, jint listflags, jstring query, jint queryId)
{
	jlong ret;
	if( query ) {
		CHAR_REF(query);
			ret = (jlong)dc_get_chatlist((dc_context_t*)hMailbox, listflags, queryPtr, queryId);
		CHAR_UNREF(query);
	}
	else {
		ret = (jlong)dc_get_chatlist((dc_context_t*)hMailbox, listflags, NULL, queryId);
	}
	return ret;
}


JNIEXPORT jlong Java_com_b44t_messenger_MrMailbox_MrMailboxGetChat(JNIEnv *env, jclass c, jlong hMailbox, jint chat_id)
{
	return (jlong)dc_get_chat((dc_context_t*)hMailbox, chat_id);
}


JNIEXPORT jint Java_com_b44t_messenger_MrMailbox_getChatIdByContactId(JNIEnv *env, jclass cls, jint contact_id)
{
	return (jint)dc_get_chat_id_by_contact_id(get_dc_context(env, cls), contact_id);
}


JNIEXPORT void Java_com_b44t_messenger_MrMailbox_markseenMsgs(JNIEnv *env, jclass cls, jintArray msg_ids)
{
	int msg_ids_cnt = 0;
	const uint32_t* msg_ids_ptr = jintArray2uint32Pointer(env, msg_ids, &msg_ids_cnt);
		dc_markseen_msgs(get_dc_context(env, cls), msg_ids_ptr, msg_ids_cnt);
	free(msg_ids_ptr);
}


JNIEXPORT void Java_com_b44t_messenger_MrMailbox_marknoticedChat(JNIEnv *env, jclass cls, jint chat_id)
{
	dc_marknoticed_chat(get_dc_context(env, cls), chat_id);
}


JNIEXPORT void Java_com_b44t_messenger_MrMailbox_marknoticedContact(JNIEnv *env, jclass cls, jint contact_id)
{
	dc_marknoticed_contact(get_dc_context(env, cls), contact_id);
}


JNIEXPORT void Java_com_b44t_messenger_MrMailbox_archiveChat(JNIEnv *env, jclass cls, jint chat_id, jint archive)
{
	dc_archive_chat(get_dc_context(env, cls), chat_id, archive);
}


JNIEXPORT jint Java_com_b44t_messenger_MrMailbox_createChatByContactId(JNIEnv *env, jclass cls, jint contact_id)
{
	return (jint)dc_create_chat_by_contact_id(get_dc_context(env, cls), contact_id);
}


JNIEXPORT jint Java_com_b44t_messenger_MrMailbox_createChatByMsgId(JNIEnv *env, jclass cls, jint msg_id)
{
	return (jint)dc_create_chat_by_msg_id(get_dc_context(env, cls), msg_id);
}


JNIEXPORT jint Java_com_b44t_messenger_MrMailbox_createGroupChat(JNIEnv *env, jclass cls, jboolean verified, jstring name)
{
	CHAR_REF(name);
		jint ret = (jint)dc_create_group_chat(get_dc_context(env, cls), verified, namePtr);
	CHAR_UNREF(name);
	return ret;
}


JNIEXPORT jint Java_com_b44t_messenger_MrMailbox_isContactInChat(JNIEnv *env, jclass cls, jint chat_id, jint contact_id)
{
	return (jint)dc_is_contact_in_chat(get_dc_context(env, cls), chat_id, contact_id);
}


JNIEXPORT jint Java_com_b44t_messenger_MrMailbox_addContactToChat(JNIEnv *env, jclass cls, jint chat_id, jint contact_id)
{
	return (jint)dc_add_contact_to_chat(get_dc_context(env, cls), chat_id, contact_id);
}


JNIEXPORT jint Java_com_b44t_messenger_MrMailbox_removeContactFromChat(JNIEnv *env, jclass cls, jint chat_id, jint contact_id)
{
	return (jint)dc_remove_contact_from_chat(get_dc_context(env, cls), chat_id, contact_id);
}


JNIEXPORT void Java_com_b44t_messenger_MrMailbox_setDraft(JNIEnv *env, jclass cls, jint chat_id, jstring draft /* NULL=delete */)
{
	CHAR_REF(draft);
		dc_set_text_draft(get_dc_context(env, cls), chat_id, draftPtr /* NULL=delete */);
	CHAR_UNREF(draft);
}


JNIEXPORT jint Java_com_b44t_messenger_MrMailbox_setChatName(JNIEnv *env, jclass cls, jint chat_id, jstring name)
{
	CHAR_REF(name);
		jint ret = (jint)dc_set_chat_name(get_dc_context(env, cls), chat_id, namePtr);
	CHAR_UNREF(name);
	return ret;
}


JNIEXPORT jint Java_com_b44t_messenger_MrMailbox_setChatProfileImage(JNIEnv *env, jclass cls, jint chat_id, jstring image/*NULL=delete*/)
{
	CHAR_REF(image);
		jint ret = (jint)dc_set_chat_profile_image(get_dc_context(env, cls), chat_id, imagePtr/*CHAR_REF() preserves NULL*/);
	CHAR_UNREF(image);
	return ret;
}


JNIEXPORT void Java_com_b44t_messenger_MrMailbox_deleteChat(JNIEnv *env, jclass cls, jint chat_id)
{
	dc_delete_chat(get_dc_context(env, cls), chat_id);
}


/* MrMailbox - handle messages */


JNIEXPORT jint Java_com_b44t_messenger_MrMailbox_getFreshMsgCount(JNIEnv *env, jclass cls, jint chat_id)
{
	return dc_get_fresh_msg_cnt(get_dc_context(env, cls), chat_id);
}


JNIEXPORT jlong Java_com_b44t_messenger_MrMailbox_MrMailboxGetMsg(JNIEnv *env, jclass c, jlong hMailbox, jint id)
{
	return (jlong)dc_get_msg((dc_context_t*)hMailbox, id);
}


JNIEXPORT jstring Java_com_b44t_messenger_MrMailbox_MrMailboxGetMsgInfo(JNIEnv *env, jclass c, jlong hMailbox, jint msg_id)
{
	char* temp = dc_get_msg_info((dc_context_t*)hMailbox, msg_id);
		jstring ret = JSTRING_NEW(temp);
	free(temp);
	return ret;
}


JNIEXPORT void Java_com_b44t_messenger_MrMailbox_deleteMsgs(JNIEnv *env, jclass cls, jintArray msg_ids)
{
	int msg_ids_cnt = 0;
	const uint32_t* msg_ids_ptr = jintArray2uint32Pointer(env, msg_ids, &msg_ids_cnt);
		dc_delete_msgs(get_dc_context(env, cls), msg_ids_ptr, msg_ids_cnt);
	free(msg_ids_ptr);
}


JNIEXPORT void Java_com_b44t_messenger_MrMailbox_forwardMsgs(JNIEnv *env, jclass cls, jintArray msg_ids, jint chat_id)
{
	int msg_ids_cnt = 0;
	const uint32_t* msg_ids_ptr = jintArray2uint32Pointer(env, msg_ids, &msg_ids_cnt);
		dc_forward_msgs(get_dc_context(env, cls), msg_ids_ptr, msg_ids_cnt, chat_id);
	free(msg_ids_ptr);
}


JNIEXPORT jint Java_com_b44t_messenger_MrMailbox_sendTextMsg(JNIEnv *env, jclass cls, jint chat_id, jstring text)
{
	CHAR_REF(text);
		jint msg_id = dc_send_text_msg(get_dc_context(env, cls), chat_id, textPtr);
	CHAR_UNREF(text);
	return msg_id;
}


static uint32_t send_image_msg(dc_context_t* context, uint32_t chat_id, const char* file, const char* filemime, int width, int height)
{
	dc_msg_t* msg = dc_msg_new(context, DC_MSG_IMAGE);
	uint32_t  ret = 0;

	if (context==NULL || chat_id<=DC_CHAT_ID_LAST_SPECIAL || file==NULL) {
		goto cleanup;
	}

	dc_msg_set_file(msg, file, NULL);
	if (width>0 && height>0) {
		dc_msg_set_dimension(msg, width, height);
	}

	ret = dc_send_msg(context, chat_id, msg);

cleanup:
	dc_msg_unref(msg);
	return ret;

}


static uint32_t send_video_msg(dc_context_t* context, uint32_t chat_id, const char* file, const char* filemime, int width, int height, int duration)
{
	dc_msg_t* msg = dc_msg_new(context, DC_MSG_VIDEO);
	uint32_t  ret = 0;

	if (context==NULL || chat_id<=DC_CHAT_ID_LAST_SPECIAL || file==NULL) {
		goto cleanup;
	}

	dc_msg_set_file(msg, file, filemime);
	if (width>0 && height>0) {
		dc_msg_set_dimension(msg, width, height);
	}
	if (duration>0) {
		dc_msg_set_duration(msg, duration);
	}

	ret = dc_send_msg(context, chat_id, msg);

cleanup:
	dc_msg_unref(msg);
	return ret;

}


static uint32_t send_voice_msg(dc_context_t* context, uint32_t chat_id, const char* file, const char* filemime, int duration)
{
	dc_msg_t* msg = dc_msg_new(context, DC_MSG_VOICE);
	uint32_t  ret = 0;

	if (context==NULL || chat_id<=DC_CHAT_ID_LAST_SPECIAL || file==NULL) {
		goto cleanup;
	}

	dc_msg_set_file(msg, file, filemime);
	if (duration>0) {
		dc_msg_set_duration(msg, duration);
	}

	ret = dc_send_msg(context, chat_id, msg);

cleanup:
	dc_msg_unref(msg);
	return ret;
}


static uint32_t send_audio_msg(dc_context_t* context, uint32_t chat_id, const char* file, const char* filemime, int duration, const char* author, const char* trackname)
{
	dc_msg_t* msg = dc_msg_new(context, DC_MSG_AUDIO);
	uint32_t ret = 0;

	if (context==NULL || chat_id<=DC_CHAT_ID_LAST_SPECIAL || file==NULL) {
		goto cleanup;
	}

	dc_msg_set_file(msg, file, filemime);
	dc_msg_set_duration(msg, duration);

	ret = dc_send_msg(context, chat_id, msg);

cleanup:
	dc_msg_unref(msg);
	return ret;
}


static uint32_t send_file_msg(dc_context_t* context, uint32_t chat_id, const char* file, const char* filemime)
{
	dc_msg_t* msg = dc_msg_new(context, DC_MSG_FILE);
	uint32_t  ret = 0;

	if (context==NULL || chat_id<=DC_CHAT_ID_LAST_SPECIAL || file==NULL) {
		goto cleanup;
	}

	dc_msg_set_file(msg, file, filemime);

	ret = dc_send_msg(context, chat_id, msg);

cleanup:
	dc_msg_unref(msg);
	return ret;
}


static uint32_t send_vcard_msg(dc_context_t* context, uint32_t chat_id, uint32_t contact_id)
{
	uint32_t      ret = 0;
	dc_contact_t* contact = NULL;
	char*         name = NULL;
	char*         addr = NULL;
	char*         text_to_send = NULL;

	if (context==NULL || chat_id<=DC_CHAT_ID_LAST_SPECIAL) {
		goto cleanup;
	}

	if ((contact=dc_get_contact(context, contact_id))==NULL) {
		goto cleanup;
	}

	name = dc_contact_get_name(contact);
	addr = dc_contact_get_addr(contact);

	if (name[0]) {
		text_to_send = dc_mprintf("%s: %s", name, addr);
	}
	else {
		text_to_send = dc_strdup(addr);
	}

	ret = dc_send_text_msg(context, chat_id, text_to_send);

cleanup:
	dc_contact_unref(contact);
	free(text_to_send);
	free(name);
	free(addr);
	return ret;
}


JNIEXPORT jint Java_com_b44t_messenger_MrMailbox_sendVcardMsg(JNIEnv *env, jobject obj, jint chat_id, jint contact_id)
{
	return send_vcard_msg(get_dc_context(env, obj), chat_id, contact_id);
}


JNIEXPORT jint Java_com_b44t_messenger_MrMailbox_sendMediaMsg(JNIEnv *env, jclass cls, jint chat_id, jint type, jstring file, jstring mime, jint w, jint h, jint ms, jstring author, jstring trackname)
{
	jint msg_id = 0;
	CHAR_REF(file);
	CHAR_REF(mime);
	CHAR_REF(author);
	CHAR_REF(trackname);
	switch( type ) {
		case DC_MSG_IMAGE: msg_id = (jint)send_image_msg(get_dc_context(env, cls), chat_id, filePtr, mimePtr, w, h); break;
		case DC_MSG_VIDEO: msg_id = (jint)send_video_msg(get_dc_context(env, cls), chat_id, filePtr, mimePtr, w, h, ms); break;
		case DC_MSG_VOICE: msg_id = (jint)send_voice_msg(get_dc_context(env, cls), chat_id, filePtr, mimePtr, ms); break;
		case DC_MSG_AUDIO: msg_id = (jint)send_audio_msg(get_dc_context(env, cls), chat_id, filePtr, mimePtr, ms, authorPtr, tracknamePtr); break;
		default:           msg_id = (jint)send_file_msg (get_dc_context(env, cls), chat_id, filePtr, mimePtr); break;
	}
	CHAR_UNREF(trackname);
	CHAR_UNREF(author);
	CHAR_UNREF(mime);
	CHAR_UNREF(file);
	return msg_id;
}


/* MrMailbox - handle config */

JNIEXPORT void Java_com_b44t_messenger_MrMailbox_setConfig(JNIEnv *env, jclass cls, jstring key, jstring value /*may be NULL*/)
{
	CHAR_REF(key);
	CHAR_REF(value);
		dc_set_config(get_dc_context(env, cls), keyPtr, valuePtr /*is NULL if value is NULL, CHAR_REF() handles this*/);
	CHAR_UNREF(key);
	CHAR_UNREF(value);
}


JNIEXPORT jstring Java_com_b44t_messenger_MrMailbox_getConfig(JNIEnv *env, jclass cls, jstring key, jstring def/*may be NULL*/)
{
	CHAR_REF(key);
		char* temp = dc_get_config(get_dc_context(env, cls), keyPtr);
			jstring ret = NULL;
			if( temp ) {
				ret = JSTRING_NEW(temp);
			}
		free(temp);
	CHAR_UNREF(key);
	return ret; /* returns NULL only if key is unset and "def" is NULL */
}


/* MrMailbox - out-of-band verification */

JNIEXPORT jlong Java_com_b44t_messenger_MrMailbox_checkQrCPtr(JNIEnv *env, jclass cls, jstring qr)
{
	CHAR_REF(qr);
		jlong ret = (jlong)dc_check_qr(get_dc_context(env, cls), qrPtr);
	CHAR_UNREF(qr);
	return ret;
}

JNIEXPORT jstring Java_com_b44t_messenger_MrMailbox_getSecurejoinQr(JNIEnv *env, jclass cls, jint chat_id)
{
	char* temp = dc_get_securejoin_qr(get_dc_context(env, cls), chat_id);
		jstring ret = JSTRING_NEW(temp);
	free(temp);
	return ret;
}

JNIEXPORT jint Java_com_b44t_messenger_MrMailbox_joinSecurejoin(JNIEnv *env, jclass cls, jstring qr)
{
	CHAR_REF(qr);
		jint ret = dc_join_securejoin(get_dc_context(env, cls), qrPtr);
	CHAR_UNREF(qr);
	return ret;
}


/* MrMailbox - misc. */

JNIEXPORT jstring Java_com_b44t_messenger_MrMailbox_getInfo(JNIEnv *env, jclass cls)
{
	char* temp = dc_get_info(get_dc_context(env, cls));
		jstring ret = JSTRING_NEW(temp);
	free(temp);
	return ret;
}


JNIEXPORT jstring Java_com_b44t_messenger_MrMailbox_getContactEncrInfo(JNIEnv *env, jclass cls, jint contact_id)
{
	char* temp = dc_get_contact_encrinfo(get_dc_context(env, cls), contact_id);
		jstring ret = JSTRING_NEW(temp);
	free(temp);
	return ret;
}


JNIEXPORT jstring Java_com_b44t_messenger_MrMailbox_cmdline(JNIEnv *env, jclass cls, jstring cmd)
{
	CHAR_REF(cmd);
		char* temp = dc_cmdline(get_dc_context(env, cls), cmdPtr);
			jstring ret = JSTRING_NEW(temp);
		free(temp);
	CHAR_UNREF(cmd);
	return ret;
}


JNIEXPORT jstring Java_com_b44t_messenger_MrMailbox_initiateKeyTransfer(JNIEnv *env, jclass cls)
{
	jstring setup_code = NULL;
	char* temp = dc_initiate_key_transfer(get_dc_context(env, cls));
	if( temp ) {
		setup_code = JSTRING_NEW(temp);
		free(temp);
	}
	return setup_code;
}


JNIEXPORT jboolean Java_com_b44t_messenger_MrMailbox_continueKeyTransfer(JNIEnv *env, jclass cls, jint msg_id, jstring setupCode)
{
	CHAR_REF(setupCode);
		jboolean ret = dc_continue_key_transfer(get_dc_context(env, cls), msg_id, setupCodePtr);
	CHAR_UNREF(setupCode);
	return ret;
}


JNIEXPORT void Java_com_b44t_messenger_MrMailbox_imex(JNIEnv *env, jclass cls, jint what, jstring dir)
{
	CHAR_REF(dir);
		dc_imex(get_dc_context(env, cls), what, dirPtr, "");
	CHAR_UNREF(dir);
}


JNIEXPORT jint Java_com_b44t_messenger_MrMailbox_checkPassword(JNIEnv *env, jclass cls, jstring pw)
{
	CHAR_REF(pw);
		jint r = dc_check_password(get_dc_context(env, cls),  pwPtr);
	CHAR_UNREF(pw);
	return r;
}


JNIEXPORT jstring Java_com_b44t_messenger_MrMailbox_imexHasBackup(JNIEnv *env, jclass cls, jstring dir)
{
	CHAR_REF(dir);
		jstring ret = NULL;
		char* temp = dc_imex_has_backup(get_dc_context(env, cls),  dirPtr);
		if( temp ) {
			ret = JSTRING_NEW(temp);
			free(temp);
		}
	CHAR_UNREF(dir);
	return ret; /* may be NULL! */
}


JNIEXPORT jint Java_com_b44t_messenger_MrMailbox_MrMailboxAddAddressBook(JNIEnv *env, jclass c, jlong hMailbox, jstring adrbook)
{
	CHAR_REF(adrbook);
		int modify_count = dc_add_address_book((dc_context_t*)hMailbox, adrbookPtr);
	CHAR_UNREF(adrbook);
	return modify_count;
}



/*******************************************************************************
 * MrChatlist
 ******************************************************************************/


JNIEXPORT void Java_com_b44t_messenger_MrChatlist_MrChatlistUnref(JNIEnv *env, jclass c, jlong hChatlist)
{
	dc_chatlist_unref((dc_chatlist_t*)hChatlist);
}


JNIEXPORT jint Java_com_b44t_messenger_MrChatlist_MrChatlistGetCnt(JNIEnv *env, jclass c, jlong hChatlist)
{
	return dc_chatlist_get_cnt((dc_chatlist_t*)hChatlist);
}


JNIEXPORT jlong Java_com_b44t_messenger_MrChatlist_MrChatlistGetChatByIndex(JNIEnv *env, jclass c, jlong hChatlist, jint index)
{
	dc_chatlist_t* chatlist = (dc_chatlist_t*)hChatlist;
	return (jlong)dc_get_chat(dc_chatlist_get_context(chatlist), dc_chatlist_get_chat_id(chatlist, index));
}


JNIEXPORT jlong Java_com_b44t_messenger_MrChatlist_MrChatlistGetMsgByIndex(JNIEnv *env, jclass c, jlong hChatlist, jint index)
{
	dc_chatlist_t* chatlist = (dc_chatlist_t*)hChatlist;
	return (jlong)dc_get_msg(dc_chatlist_get_context(chatlist), dc_chatlist_get_msg_id(chatlist, index));
}


JNIEXPORT jlong Java_com_b44t_messenger_MrChatlist_MrChatlistGetSummaryByIndex(JNIEnv *env, jclass c, jlong hChatlist, jint index, jlong hChat)
{
	return (jlong)dc_chatlist_get_summary((dc_chatlist_t*)hChatlist, index, (dc_chat_t*)hChat);
}


/*******************************************************************************
 * MrChat
 ******************************************************************************/


static dc_chat_t* get_dc_chat(JNIEnv *env, jobject obj)
{
	static jfieldID fid = 0;
	if( fid == 0 ) {
		jclass cls = (*env)->GetObjectClass(env, obj);
		fid = (*env)->GetFieldID(env, cls, "m_hChat", "J" /*Signature, J=long*/);
	}
	if( fid ) {
		return (dc_chat_t*)(*env)->GetLongField(env, obj, fid);
	}
	return NULL;
}


JNIEXPORT void Java_com_b44t_messenger_MrChat_MrChatUnref(JNIEnv *env, jclass c, jlong hChat)
{
	dc_chat_unref((dc_chat_t*)hChat);
}


JNIEXPORT jint Java_com_b44t_messenger_MrChat_getId(JNIEnv *env, jobject obj)
{
	return dc_chat_get_id(get_dc_chat(env, obj));
}


JNIEXPORT jboolean Java_com_b44t_messenger_MrChat_isGroup(JNIEnv *env, jobject obj)
{
	int chat_type = dc_chat_get_type(get_dc_chat(env, obj));
	return (chat_type==DC_CHAT_TYPE_GROUP || chat_type==DC_CHAT_TYPE_VERIFIED_GROUP);
}


JNIEXPORT jint Java_com_b44t_messenger_MrChat_getArchived(JNIEnv *env, jobject obj)
{
	return dc_chat_get_archived(get_dc_chat(env, obj));
}


JNIEXPORT jstring Java_com_b44t_messenger_MrChat_getName(JNIEnv *env, jobject obj)
{
	const char* temp = dc_chat_get_name(get_dc_chat(env, obj));
		jstring ret = JSTRING_NEW(temp);
	free(temp);
	return ret;
}


JNIEXPORT jstring Java_com_b44t_messenger_MrChat_getSubtitle(JNIEnv *env, jobject obj)
{
	const char* temp = dc_chat_get_subtitle(get_dc_chat(env, obj));
		jstring ret = JSTRING_NEW(temp);
	free(temp);
	return ret;
}


JNIEXPORT jstring Java_com_b44t_messenger_MrChat_getProfileImage(JNIEnv *env, jobject obj)
{
	const char* temp = dc_chat_get_profile_image(get_dc_chat(env, obj));
		jstring ret = JSTRING_NEW(temp);
	free(temp);
	return ret;
}


JNIEXPORT jboolean Java_com_b44t_messenger_MrChat_isUnpromoted(JNIEnv *env, jobject obj)
{
	return dc_chat_is_unpromoted(get_dc_chat(env, obj)) != 0;
}


JNIEXPORT jboolean Java_com_b44t_messenger_MrChat_isSelfTalk(JNIEnv *env, jobject obj)
{
	return dc_chat_is_self_talk(get_dc_chat(env, obj)) != 0;
}


JNIEXPORT jboolean Java_com_b44t_messenger_MrChat_isVerified(JNIEnv *env, jobject obj)
{
	return dc_chat_is_verified(get_dc_chat(env, obj)) != 0;
}


JNIEXPORT jstring Java_com_b44t_messenger_MrChat_getDraft(JNIEnv *env, jobject obj) /* returns NULL for "no draft" */
{
	const char* temp = dc_chat_get_text_draft(get_dc_chat(env, obj));
		jstring ret = temp? JSTRING_NEW(temp) : NULL;
	free(temp);
	return ret;
}


JNIEXPORT jlong Java_com_b44t_messenger_MrChat_getDraftTimestamp(JNIEnv *env, jobject obj)
{
	return (jlong)dc_chat_get_draft_timestamp(get_dc_chat(env, obj));
}


JNIEXPORT jintArray Java_com_b44t_messenger_MrMailbox_getChatMedia(JNIEnv *env, jclass cls, jint chat_id, jint msg_type, jint or_msg_type)
{
	dc_array_t* ca = dc_get_chat_media(get_dc_context(env, cls), chat_id, msg_type, or_msg_type);
	return dc_array2jintArray_n_unref(env, ca);
}


JNIEXPORT jint Java_com_b44t_messenger_MrMailbox_getNextMedia(JNIEnv *env, jclass cls, jint msg_id, jint dir)
{
	return dc_get_next_media(get_dc_context(env, cls), msg_id, dir);
}


JNIEXPORT jintArray Java_com_b44t_messenger_MrMailbox_getChatMsgs(JNIEnv *env, jclass cls, jint chat_id, jint flags, jint marker1before)
{
	dc_array_t* ca = dc_get_chat_msgs(get_dc_context(env, cls), chat_id, flags, marker1before);
	return dc_array2jintArray_n_unref(env, ca);
}


JNIEXPORT jintArray Java_com_b44t_messenger_MrMailbox_searchMsgs(JNIEnv *env, jclass cls, jint chat_id, jstring query)
{
	CHAR_REF(query);
		dc_array_t* ca = dc_search_msgs(get_dc_context(env, cls), chat_id, queryPtr);
	CHAR_UNREF(query);
	return dc_array2jintArray_n_unref(env, ca);
}


JNIEXPORT jintArray Java_com_b44t_messenger_MrMailbox_getFreshMsgs(JNIEnv *env, jclass cls)
{
	dc_array_t* ca = dc_get_fresh_msgs(get_dc_context(env, cls));
	return dc_array2jintArray_n_unref(env, ca);
}


JNIEXPORT jintArray Java_com_b44t_messenger_MrMailbox_getChatContacts(JNIEnv *env, jclass cls, jint chat_id)
{
	dc_array_t* ca = dc_get_chat_contacts(get_dc_context(env, cls), chat_id);
	return dc_array2jintArray_n_unref(env, ca);
}


/*******************************************************************************
 * MrMsg
 ******************************************************************************/


static dc_msg_t* get_dc_msg(JNIEnv *env, jobject obj)
{
	static jfieldID fid = 0;
	if( fid == 0 ) {
		jclass cls = (*env)->GetObjectClass(env, obj);
		fid = (*env)->GetFieldID(env, cls, "m_hMsg", "J" /*Signature, J=long*/);
	}
	if( fid ) {
		return (dc_msg_t*)(*env)->GetLongField(env, obj, fid);
	}
	return NULL;
}


JNIEXPORT void Java_com_b44t_messenger_MrMsg_MrMsgUnref(JNIEnv *env, jclass c, jlong hMsg)
{
	dc_msg_unref((dc_msg_t*)hMsg);
}


JNIEXPORT jint Java_com_b44t_messenger_MrMsg_getId(JNIEnv *env, jobject obj)
{
	return dc_msg_get_id(get_dc_msg(env, obj));
}


JNIEXPORT jstring Java_com_b44t_messenger_MrMsg_getText(JNIEnv *env, jobject obj)
{
	char* temp = dc_msg_get_text(get_dc_msg(env, obj));
		jstring ret = JSTRING_NEW(temp);
	free(temp);
	return ret;
}


JNIEXPORT jlong Java_com_b44t_messenger_MrMsg_getTimestamp(JNIEnv *env, jobject obj)
{
	return (jlong)dc_msg_get_timestamp(get_dc_msg(env, obj));
}


JNIEXPORT jint Java_com_b44t_messenger_MrMsg_getType(JNIEnv *env, jobject obj)
{
	return dc_msg_get_viewtype(get_dc_msg(env, obj));
}


JNIEXPORT jint Java_com_b44t_messenger_MrMsg_getState(JNIEnv *env, jobject obj)
{
	return dc_msg_get_state(get_dc_msg(env, obj));
}


JNIEXPORT jint Java_com_b44t_messenger_MrMsg_getChatId(JNIEnv *env, jobject obj)
{
	return dc_msg_get_chat_id(get_dc_msg(env, obj));
}


JNIEXPORT jint Java_com_b44t_messenger_MrMsg_getFromId(JNIEnv *env, jobject obj)
{
	return dc_msg_get_from_id(get_dc_msg(env, obj));
}


JNIEXPORT jint Java_com_b44t_messenger_MrMsg_getWidth(JNIEnv *env, jobject obj, jint def)
{
	jint ret = (jint)dc_msg_get_width(get_dc_msg(env, obj));
	return ret? ret : def;
}


JNIEXPORT jint Java_com_b44t_messenger_MrMsg_getHeight(JNIEnv *env, jobject obj, jint def)
{
	jint ret = (jint)dc_msg_get_height(get_dc_msg(env, obj));
	return ret? ret : def;
}


JNIEXPORT jint Java_com_b44t_messenger_MrMsg_getDuration(JNIEnv *env, jobject obj)
{
	return dc_msg_get_duration(get_dc_msg(env, obj));
}


JNIEXPORT void Java_com_b44t_messenger_MrMsg_lateFilingMediaSize(JNIEnv *env, jobject obj, jint width, jint height, jint duration)
{
	dc_msg_latefiling_mediasize(get_dc_msg(env, obj), width, height, duration);
}


JNIEXPORT jint Java_com_b44t_messenger_MrMsg_getBytes(JNIEnv *env, jobject obj)
{
	return (jint)dc_msg_get_filebytes(get_dc_msg(env, obj));
}


JNIEXPORT jlong Java_com_b44t_messenger_MrMsg_getSummaryCPtr(JNIEnv *env, jobject obj, jlong hChat)
{
	return (jlong)dc_msg_get_summary(get_dc_msg(env, obj), (dc_chat_t*)hChat);
}


JNIEXPORT jstring Java_com_b44t_messenger_MrMsg_getSummarytext(JNIEnv *env, jobject obj, jint approx_characters)
{
	char* temp = dc_msg_get_summarytext(get_dc_msg(env, obj), approx_characters);
		jstring ret = JSTRING_NEW(temp);
	free(temp);
	return ret;
}


JNIEXPORT jint Java_com_b44t_messenger_MrMsg_showPadlock(JNIEnv *env, jobject obj)
{
	return dc_msg_get_showpadlock(get_dc_msg(env, obj));
}


JNIEXPORT jstring Java_com_b44t_messenger_MrMsg_getFile(JNIEnv *env, jobject obj)
{
	char* temp = dc_msg_get_file(get_dc_msg(env, obj));
		jstring ret =  JSTRING_NEW(temp);
	free(temp);
	return ret;
}


JNIEXPORT jstring Java_com_b44t_messenger_MrMsg_getFilemime(JNIEnv *env, jobject obj)
{
	char* temp = dc_msg_get_filemime(get_dc_msg(env, obj));
		jstring ret =  JSTRING_NEW(temp);
	free(temp);
	return ret;
}


JNIEXPORT jstring Java_com_b44t_messenger_MrMsg_getFilename(JNIEnv *env, jobject obj)
{
	char* temp = dc_msg_get_filename(get_dc_msg(env, obj));
		jstring ret =  JSTRING_NEW(temp);
	free(temp);
	return ret;
}


JNIEXPORT jboolean Java_com_b44t_messenger_MrMsg_isForwarded(JNIEnv *env, jobject obj)
{
    return dc_msg_is_forwarded(get_dc_msg(env, obj)) != 0;
}


JNIEXPORT jboolean Java_com_b44t_messenger_MrMsg_isIncreation(JNIEnv *env, jobject obj)
{
    return dc_msg_is_increation(get_dc_msg(env, obj)) != 0;
}


JNIEXPORT jboolean Java_com_b44t_messenger_MrMsg_isInfo(JNIEnv *env, jobject obj)
{
    return dc_msg_is_info(get_dc_msg(env, obj)) != 0;
}


JNIEXPORT jboolean Java_com_b44t_messenger_MrMsg_isSetupMessage(JNIEnv *env, jobject obj)
{
    return dc_msg_is_setupmessage(get_dc_msg(env, obj));
}


JNIEXPORT jstring Java_com_b44t_messenger_MrMsg_getSetupCodeBegin(JNIEnv *env, jobject obj)
{
	char* temp = dc_msg_get_setupcodebegin(get_dc_msg(env, obj));
		jstring ret =  JSTRING_NEW(temp);
	free(temp);
	return ret;
}



/*******************************************************************************
 * MrContact
 ******************************************************************************/


static dc_contact_t* get_dc_contact(JNIEnv *env, jobject obj)
{
	static jfieldID fid = 0;
	if( fid == 0 ) {
		jclass cls = (*env)->GetObjectClass(env, obj);
		fid = (*env)->GetFieldID(env, cls, "m_hContact", "J" /*Signature, J=long*/);
	}
	if( fid ) {
		return (dc_contact_t*)(*env)->GetLongField(env, obj, fid);
	}
	return NULL;
}


JNIEXPORT void Java_com_b44t_messenger_MrContact_MrContactUnref(JNIEnv *env, jclass c, jlong hContact)
{
	dc_contact_unref((dc_contact_t*)hContact);
}


JNIEXPORT jstring Java_com_b44t_messenger_MrContact_getName(JNIEnv *env, jobject obj)
{
	const char* temp = dc_contact_get_name(get_dc_contact(env, obj));
		jstring ret = JSTRING_NEW(temp);
	free(temp);
	return ret;
}


JNIEXPORT jstring Java_com_b44t_messenger_MrContact_getDisplayName(JNIEnv *env, jobject obj)
{
	const char* temp = dc_contact_get_display_name(get_dc_contact(env, obj));
		jstring ret = JSTRING_NEW(temp);
	free(temp);
	return ret;
}


JNIEXPORT jstring Java_com_b44t_messenger_MrContact_getFirstName(JNIEnv *env, jobject obj)
{
	const char* temp = dc_contact_get_first_name(get_dc_contact(env, obj));
		jstring ret = JSTRING_NEW(temp);
	free(temp);
	return ret;
}


JNIEXPORT jstring Java_com_b44t_messenger_MrContact_getAddr(JNIEnv *env, jobject obj)
{
	const char* temp = dc_contact_get_addr(get_dc_contact(env, obj));
		jstring ret = JSTRING_NEW(temp);
	free(temp);
	return ret;
}


JNIEXPORT jstring Java_com_b44t_messenger_MrContact_getNameNAddr(JNIEnv *env, jobject obj)
{
	const char* temp = dc_contact_get_name_n_addr(get_dc_contact(env, obj));
		jstring ret = JSTRING_NEW(temp);
	free(temp);
	return ret;
}


JNIEXPORT jboolean Java_com_b44t_messenger_MrContact_isBlocked(JNIEnv *env, jobject obj)
{
	return (jboolean)( dc_contact_is_blocked(get_dc_contact(env, obj)) != 0 );
}


JNIEXPORT jboolean Java_com_b44t_messenger_MrContact_isVerified(JNIEnv *env, jobject obj)
{
	return dc_contact_is_verified(get_dc_contact(env, obj)) == 2;
}


/*******************************************************************************
 * MrLot
 ******************************************************************************/


static dc_lot_t* get_dc_lot(JNIEnv *env, jobject obj)
{
	static jfieldID fid = 0;
	if( fid == 0 ) {
		jclass cls = (*env)->GetObjectClass(env, obj);
		fid = (*env)->GetFieldID(env, cls, "m_hLot", "J" /*Signature, J=long*/);
	}
	if( fid ) {
		return (dc_lot_t*)(*env)->GetLongField(env, obj, fid);
	}
	return NULL;
}


JNIEXPORT void Java_com_b44t_messenger_MrLot_unref(JNIEnv *env, jclass cls, jlong hLot)
{
	dc_lot_unref((dc_lot_t*)hLot);
}


JNIEXPORT jstring Java_com_b44t_messenger_MrLot_getText1(JNIEnv *env, jobject obj)
{
	char* temp = dc_lot_get_text1(get_dc_lot(env, obj));
		jstring ret = JSTRING_NEW(temp);
	free(temp);
	return ret;
}


JNIEXPORT jint Java_com_b44t_messenger_MrLot_getText1Meaning(JNIEnv *env, jobject obj)
{
	return dc_lot_get_text1_meaning(get_dc_lot(env, obj));
}


JNIEXPORT jstring Java_com_b44t_messenger_MrLot_getText2(JNIEnv *env, jobject obj)
{
	char* temp = dc_lot_get_text2(get_dc_lot(env, obj));
		jstring ret = JSTRING_NEW(temp);
	free(temp);
	return ret;
}


JNIEXPORT jlong Java_com_b44t_messenger_MrLot_getTimestamp(JNIEnv *env, jobject obj)
{
	return dc_lot_get_timestamp(get_dc_lot(env, obj));
}


JNIEXPORT jint Java_com_b44t_messenger_MrLot_getState(JNIEnv *env, jobject obj)
{
	return dc_lot_get_state(get_dc_lot(env, obj));
}


JNIEXPORT jint Java_com_b44t_messenger_MrLot_getId(JNIEnv *env, jobject obj)
{
	return dc_lot_get_id(get_dc_lot(env, obj));
}


JNIEXPORT void Java_com_b44t_messenger_MrLot_MrLotUnref(JNIEnv *env, jclass c, jlong hLot)
{
	dc_lot_unref((dc_lot_t*)hLot);
}


/*******************************************************************************
 * Tools
 ******************************************************************************/


JNIEXPORT jstring Java_com_b44t_messenger_MrMailbox_CPtr2String(JNIEnv *env, jclass c, jlong hStr)
{
	/* the callback may return a long that represents a pointer to a C-String; this function creates a Java-string from such values. */
	if( hStr == 0 ) {
		return NULL;
	}
	const char* ptr = (const char*)hStr;
	return JSTRING_NEW(ptr);
}


JNIEXPORT jlong Java_com_b44t_messenger_MrMailbox_String2CPtr(JNIEnv *env, jclass c, jstring str)
{
    char* hStr = NULL;
    if( str ) {
        CHAR_REF(str);
            hStr = strdup(strPtr);
        CHAR_UNREF(str);
    }
    return (jlong)hStr;
}


JNIEXPORT jstring Java_com_b44t_messenger_MrMailbox_MrGetVersionStr(JNIEnv *env, jclass c)
{
	s_init_globals(env, c);
	const char* temp = dc_get_version_str();
		jstring ret = JSTRING_NEW(temp);
	free(temp);
	return ret;
}


#include <time.h>
JNIEXPORT jlong Java_com_b44t_messenger_MrMailbox_getCurrentTimeMillis(JNIEnv *env, jclass c) {
	struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (jlong) ts.tv_sec * 1000 + (int64_t) ts.tv_nsec / 1000000;
}
JNIEXPORT jint Java_com_b44t_messenger_MrMailbox_getCurrentTime(JNIEnv *env, jclass c) {
    return (jint) (Java_com_b44t_messenger_MrMailbox_getCurrentTimeMillis(env, c) / 1000);
}


