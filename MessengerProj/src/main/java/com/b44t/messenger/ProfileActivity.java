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
 ******************************************************************************/


package com.b44t.messenger;

import android.app.AlertDialog;
import android.app.Dialog;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.res.Configuration;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;

import android.os.Build;
import android.os.Bundle;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewTreeObserver;
import android.widget.FrameLayout;
import android.widget.Toast;

import com.b44t.messenger.Cells.ChatlistCell;
import com.b44t.messenger.Cells.HeaderCell;
import com.b44t.messenger.Cells.TextInfoCell;
import com.b44t.messenger.Cells.TextSettingsCell;
import com.b44t.messenger.aosp.LinearLayoutManager;
import com.b44t.messenger.aosp.RecyclerView;

import com.b44t.messenger.ActionBar.BackDrawable;
import com.b44t.messenger.ActionBar.SimpleTextView;
import com.b44t.messenger.Cells.ShadowSectionCell;
import com.b44t.messenger.Cells.UserCell;
import com.b44t.messenger.ActionBar.ActionBar;
import com.b44t.messenger.Components.AvatarDrawable;
import com.b44t.messenger.Components.AvatarUpdater;
import com.b44t.messenger.Components.BackupImageView;
import com.b44t.messenger.ActionBar.BaseFragment;
import com.b44t.messenger.Components.LayoutHelper;
import com.b44t.messenger.Components.RecyclerListView;
import com.b44t.messenger.ActionBar.Theme;

import java.io.File;


public class ProfileActivity extends BaseFragment implements NotificationCenter.NotificationCenterDelegate, PhotoViewer.PhotoViewerProvider {

    private int user_id;  // show the profile of a single user
    private int chat_id;  // show the profile of a group

    private final int ROWTYPE_TEXT_SETTINGS = 2;
    private final int ROWTYPE_CONTACT = 3;
    private final int ROWTYPE_CHAT = 4;
    private final int ROWTYPE_SHADOW = 5;
    private final int ROWTYPE_INFO = 6;
    private final int ROWTYPE_HEADER = 7;

    private RecyclerListView listView;
    private ListAdapter listAdapter;
    private BackupImageView avatarImage;
    private SimpleTextView nameTextView;
    private SimpleTextView subtitleTextView;
    private AvatarDrawable avatarDrawable;
    private TopView topView;

    private int extraHeight;

    private AvatarUpdater avatarUpdater;

    private int changeNameRow = -1;
    private int settingsRow = -1;
    private int settingsShadowRow = -1;

    private int emailHeaderRow = -1;
    private int emailRow = -1;
    private int infoRow = -1;
    private int listShadowRow = -1;

    private int[] memberlistUserIds;
    private int memberlistHeaderRow = -1;
    private int memberlistFirstRow = -1;
    private int memberlistLastRow = -1;
    private int addMemberRow = -1;
    private int showQrRow = -1;

    private MrChatlist chatlist = new MrChatlist(0);
    private int chatlistHeaderRow = -1;
    private int chatlistFirstRow = -1;
    private int chatlistLastRow = -1;
    private int startChatRow = -1;

    private int otherShadowRow = -1;
    private int otherHeaderRow = -1;
    private int addShortcutRow = -1;
    private int blockContactRow = -1; // only block, no deletion here: contacts shown here are typically in use; we cannot easily delete them and mrmailbox_delete_contact() will typically fail.
                                      // a delete function is available in the contact list, however, where the chance a contact can be deleted is larger
    private int rowCount = 0;

    private class TopView extends View {

        private Paint paint = new Paint();

        public TopView(Context context) {
            super(context);
        }

        @Override
        protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
            setMeasuredDimension(MeasureSpec.getSize(widthMeasureSpec), ActionBar.getCurrentActionBarHeight() + (actionBar.getOccupyStatusBar() ? AndroidUtilities.statusBarHeight : 0) + AndroidUtilities.dp(91));
        }

        @Override
        public void setBackgroundColor(int color) {
            paint.setColor(color);
            invalidate();
        }

        @Override
        protected void onDraw(Canvas canvas) {
            int height = getMeasuredHeight() - AndroidUtilities.dp(91);
            canvas.drawRect(0, 0, getMeasuredWidth(), height + extraHeight, paint);
            if (parentLayout != null) {
                parentLayout.drawHeaderShadow(canvas, height + extraHeight);
            }
        }
    }

    public ProfileActivity(Bundle args) {
        super(args);
    }

    @Override
    public boolean onFragmentCreate() {
        user_id = arguments.getInt("user_id", 0);
        chat_id = getArguments().getInt("chat_id", 0);
        if (user_id != 0) {
            chatlist = MrMailbox.getChatlist(0, null, user_id);

            TLRPC.User user = MrMailbox.getUser(user_id);
            if (user == null) {
                return false;
            }
            NotificationCenter.getInstance().addObserver(this, NotificationCenter.contactsDidLoaded);
            NotificationCenter.getInstance().addObserver(this, NotificationCenter.dialogsNeedReload);
            NotificationCenter.getInstance().addObserver(this, NotificationCenter.notificationsSettingsUpdated);
            NotificationCenter.getInstance().addObserver(this, NotificationCenter.messageSendError);
        } else if (chat_id != 0) {
            memberlistUserIds = MrMailbox.getChatContacts(chat_id);

            avatarUpdater = new AvatarUpdater();
            avatarUpdater.returnOnly = true;
            avatarUpdater.delegate = new AvatarUpdater.AvatarUpdaterDelegate() {
                @Override
                public void didUploadedPhoto(TLRPC.InputFile file, TLRPC.PhotoSize small, TLRPC.PhotoSize big) {
                    if (user_id==0 && chat_id != 0 ) {
                        String nameonly = big.location.volume_id + "_" + big.location.local_id + ".jpg";
                        File fileobj = new File(FileLoader.getInstance().getDirectory(FileLoader.MEDIA_DIR_CACHE), nameonly);
                        String fullpath = fileobj.getAbsolutePath();
                        MrMailbox.setChatProfileImage(chat_id, fullpath);
                    }
                }
            };
            avatarUpdater.parentFragment = this;
        } else {
            return false;
        }

        NotificationCenter.getInstance().addObserver(this, NotificationCenter.updateInterfaces);
        NotificationCenter.getInstance().addObserver(this, NotificationCenter.closeChats);
        updateRowsIds();

        return true;
    }

    @Override
    public void onFragmentDestroy() {
        super.onFragmentDestroy();
        NotificationCenter.getInstance().removeObserver(this, NotificationCenter.updateInterfaces);
        NotificationCenter.getInstance().removeObserver(this, NotificationCenter.closeChats);
        if (user_id != 0) {
            NotificationCenter.getInstance().removeObserver(this, NotificationCenter.contactsDidLoaded);
            NotificationCenter.getInstance().removeObserver(this, NotificationCenter.dialogsNeedReload);
            NotificationCenter.getInstance().removeObserver(this, NotificationCenter.notificationsSettingsUpdated);
            NotificationCenter.getInstance().removeObserver(this, NotificationCenter.messageSendError);
        } else if (chat_id != 0) {
            avatarUpdater.clear();
        }
    }

    @Override
    protected ActionBar createActionBar(Context context) {
        ActionBar actionBar = new ActionBar(context) {
            @Override
            public boolean onTouchEvent(MotionEvent event) {
                return super.onTouchEvent(event);
            }
        };
        actionBar.setBackButtonDrawable(new BackDrawable(false));
        actionBar.setCastShadows(false);
        actionBar.setAddToContainer(false);
        actionBar.setOccupyStatusBar(Build.VERSION.SDK_INT >= 21);
        return actionBar;
    }

    private static final int ANIM_OFF = 8; // a correction value to compensate changes in AVATAR_AFTER_BACK_X

    @Override
    public View createView(final Context context) {
        hasOwnBackground = true;
        extraHeight = AndroidUtilities.dp(88);
        actionBar.setActionBarMenuOnItemClick(new ActionBar.ActionBarMenuOnItemClick() {
            @Override
            public void onItemClick(final int id) {
                if (getParentActivity() == null) {
                    return;
                }
                if (id == -1) {
                    finishFragment();
                }
            }
        });

        listAdapter = new ListAdapter(context);
        avatarDrawable = new AvatarDrawable();

        fragmentView = new FrameLayout(context) {
            @Override
            public boolean hasOverlappingRendering() {
                return false;
            }

            @Override
            protected void onLayout(boolean changed, int left, int top, int right, int bottom) {
                super.onLayout(changed, left, top, right, bottom);
                checkListViewScroll();
            }
        };
        FrameLayout frameLayout = (FrameLayout) fragmentView;

        listView = new RecyclerListView(context) {
            @Override
            public boolean hasOverlappingRendering() {
                return false;
            }
        };
        listView.setTag(6);
        listView.setPadding(0, AndroidUtilities.dp(88), 0, 0);
        listView.setBackgroundColor(0xffffffff);
        listView.setItemAnimator(null);
        listView.setLayoutAnimation(null);
        listView.setClipToPadding(false);
        LinearLayoutManager layoutManager = new LinearLayoutManager(context) {
            @Override
            public boolean supportsPredictiveItemAnimations() {
                return false;
            }
        };
        layoutManager.setOrientation(LinearLayoutManager.VERTICAL);
        listView.setLayoutManager(layoutManager);
        listView.setGlowColor(Theme.ACTION_BAR_COLOR);
        frameLayout.addView(listView, LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, LayoutHelper.MATCH_PARENT, Gravity.TOP | Gravity.START));

        listView.setAdapter(listAdapter);
        listView.setOnItemClickListener(new RecyclerListView.OnItemClickListener() {
            @Override
            public void onItemClick(View view, final int position) {
                if (getParentActivity() == null)
                {
                    return;
                }

                if (position == settingsRow)
                {
                    Bundle args = new Bundle();
                    if (chat_id != 0) {
                        args.putInt("chat_id", chat_id);
                    } else {
                        args.putInt("chat_id", MrMailbox.getChatIdByContactId(user_id));
                    }
                    presentFragment(new ProfileNotificationsActivity(args));
                }
                else if(position==changeNameRow)
                {
                    Bundle args = new Bundle();
                    args.putInt("do_what", ContactAddActivity.EDIT_NAME);
                    if (chat_id != 0) {
                        args.putInt("chat_id", chat_id);
                    } else {
                        args.putInt("user_id", user_id);
                    }
                    presentFragment(new ContactAddActivity(args));
                }
                else if( position == emailRow )
                {
                    final String addr = MrMailbox.getContact(user_id).getAddr();
                    AlertDialog.Builder builder = new AlertDialog.Builder(getParentActivity()); // was: BottomSheet.Builder
                    builder.setTitle(addr);
                    CharSequence[] items = new CharSequence[]{ApplicationLoader.applicationContext.getString(R.string.CopyToClipboard)};
                    builder.setItems(items, new DialogInterface.OnClickListener() {
                                @Override
                                public void onClick(DialogInterface dialogInterface, int i) {
                                    AndroidUtilities.addToClipboard(addr);
                                    AndroidUtilities.showDoneHint(ApplicationLoader.applicationContext);
                                }
                            }
                    );
                    showDialog(builder.create());
                }
                else if( position == infoRow ) {
                    String info_str = MrMailbox.getContactEncrInfo(user_id);
                    AlertDialog.Builder builder = new AlertDialog.Builder(getParentActivity());
                    builder.setTitle(R.string.Encryption);
                    builder.setMessage(AndroidUtilities.replaceTags(info_str));
                    builder.setPositiveButton(R.string.OK, null);
                    showDialog(builder.create());
                }
                else if(position==startChatRow)
                {
                    int belonging_chat_id = MrMailbox.getChatIdByContactId(user_id);
                    if( belonging_chat_id != 0 ) {
                        Bundle args = new Bundle();
                        args.putInt("chat_id", belonging_chat_id);
                        NotificationCenter.getInstance().removeObserver(ProfileActivity.this, NotificationCenter.closeChats);
                        NotificationCenter.getInstance().postNotificationName(NotificationCenter.closeChats);
                        presentFragment(new ChatActivity(args), true /*remove last*/);
                        return;
                    }

                    String name = MrMailbox.getContact(user_id).getNameNAddr();
                    AlertDialog.Builder builder = new AlertDialog.Builder(getParentActivity());
                    builder.setPositiveButton(R.string.OK, new DialogInterface.OnClickListener() {
                        @Override
                        public void onClick(DialogInterface dialogInterface, int i) {
                            int belonging_chat_id = MrMailbox.createChatByContactId(user_id);
                            if( belonging_chat_id != 0 ) {
                                Bundle args = new Bundle();
                                args.putInt("chat_id", belonging_chat_id);
                                NotificationCenter.getInstance().removeObserver(ProfileActivity.this, NotificationCenter.closeChats);
                                NotificationCenter.getInstance().postNotificationName(NotificationCenter.closeChats);
                                presentFragment(new ChatActivity(args), true /*remove last*/);
                                return;
                            }
                        }
                    });
                    builder.setNegativeButton(R.string.Cancel, null);
                    builder.setMessage(AndroidUtilities.replaceTags(String.format(context.getString(R.string.AskStartChatWith), name)));
                    showDialog(builder.create());
                }
                else if (position == addMemberRow)
                {
                    Bundle args = new Bundle();
                    args.putInt("do_what", MrMailbox.getChat(chat_id).isVerified()? ContactsActivity.ADD_CONTACTS_TO_VERIFIED_GROUP : ContactsActivity.ADD_CONTACTS_TO_GROUP);
                    ContactsActivity fragment = new ContactsActivity(args);
                    fragment.setDelegate(new ContactsActivity.ContactsActivityDelegate() {
                        @Override
                        public void didSelectContact(final int added_user_id) {
                            if( MrMailbox.isContactInChat(chat_id, added_user_id)!=0 )
                            {
                                Toast.makeText(getParentActivity(), context.getString(R.string.ContactAlreadyInGroup), Toast.LENGTH_SHORT).show();
                            }
                            else {
                                AlertDialog.Builder builder = new AlertDialog.Builder(getParentActivity());
                                builder.setPositiveButton(R.string.OK, new DialogInterface.OnClickListener() {
                                    @Override
                                    public void onClick(DialogInterface dialogInterface, int i) {
                                        MrMailbox.addContactToChat(chat_id, added_user_id);
                                        NotificationCenter.getInstance().postNotificationName(NotificationCenter.updateInterfaces, MrMailbox.UPDATE_MASK_CHAT_MEMBERS);
                                    }
                                });
                                builder.setNegativeButton(R.string.Cancel, null);
                                String name = MrMailbox.getContact(added_user_id).getDisplayName();
                                builder.setMessage(AndroidUtilities.replaceTags(String.format(context.getString(R.string.AskAddMemberToGroup), name)));
                                showDialog(builder.create());
                            }
                        }
                    });
                    presentFragment(fragment);
                }
                else if( position == showQrRow )
                {
                    Intent intent2 = new Intent(getParentActivity(), QRshowActivity.class);
                    Bundle b = new Bundle();
                    b.putInt("chat_id", chat_id);
                    intent2.putExtras(b);
                    getParentActivity().startActivity(intent2);
                }
                else if (position == blockContactRow)
                {
                    if( userBlocked() ) {
                        MrMailbox.blockContact(user_id, 0);
                        finishFragment(); /* got to the parent, this is important eg. when editing blocking in the BlockedUserActivitiy. Moreover, this saves us updating all the states in the profile */
                    }
                    else {
                        AlertDialog.Builder builder = new AlertDialog.Builder(getParentActivity());
                        builder.setMessage(context.getString(R.string.AreYouSureBlockContact));
                        builder.setPositiveButton(R.string.OK, new DialogInterface.OnClickListener() {
                            @Override
                            public void onClick(DialogInterface dialogInterface, int i) {
                                MrMailbox.blockContact(user_id, 1);
                                finishFragment(); /* got to the parent, this is important eg. when editing blocking in the BlockedUserActivitiy. Moreover, this saves us updating all the states in the profile */
                            }
                        });
                        builder.setNegativeButton(R.string.Cancel, null);
                        showDialog(builder.create());
                    }
                }
                else if (position == addShortcutRow)
                {
                    try {
                        // draw avatar into a bitmap
                        int wh = avatarImage.imageReceiver.getImageWidth();
                        Bitmap bitmap = Bitmap.createBitmap(wh, wh, Bitmap.Config.ARGB_8888);
                        bitmap.eraseColor(Color.TRANSPARENT);
                        Canvas canvas = new Canvas(bitmap);
                        avatarImage.imageReceiver.draw(canvas);

                        // add shortcut
                        int install_chat_id = chat_id!=0? chat_id : MrMailbox.getChatIdByContactId(user_id);
                        AndroidUtilities.installShortcut(install_chat_id, bitmap);
                        Toast.makeText(getParentActivity(), context.getString(R.string.ShortcutAdded), Toast.LENGTH_LONG).show();

                    } catch (Exception e) {

                    }
                }
                else if (position >= memberlistFirstRow && position <= memberlistLastRow)
                {
                    int curr_user_index = position - memberlistFirstRow;
                    if(curr_user_index>=0 && curr_user_index< memberlistUserIds.length) {
                        int curr_user_id = memberlistUserIds[curr_user_index];
                        if( curr_user_id == MrContact.MR_CONTACT_ID_SELF
                         || curr_user_id > MrContact.MR_CONTACT_ID_LAST_SPECIAL ) {
                            Bundle args = new Bundle();
                            args.putInt("user_id", curr_user_id);
                            presentFragment(new ProfileActivity(args));
                        }
                    }
                }
                else if( position >= chatlistFirstRow && position<=chatlistLastRow) {
                    int curr_chatlist_index = position - chatlistFirstRow;
                    if(curr_chatlist_index>=0 && curr_chatlist_index< chatlist.getCnt()) {
                        Bundle args = new Bundle();
                        args.putInt("chat_id", chatlist.getChatByIndex(curr_chatlist_index).getId());
                        presentFragment(new ChatActivity(args));
                    }
                }
            }
        });

        listView.setOnItemLongClickListener(new RecyclerListView.OnItemLongClickListener() {
            @Override
            public boolean onItemClick(View view, int position) {
                if (position >= memberlistFirstRow && position <= memberlistLastRow)
                {
                    int curr_user_index = position - memberlistFirstRow;
                    if(curr_user_index>=0 && curr_user_index< memberlistUserIds.length)
                    {
                        final int curr_user_id = memberlistUserIds[curr_user_index];
                        AlertDialog.Builder builder = new AlertDialog.Builder(getParentActivity());
                        CharSequence[] items = new CharSequence[]{context.getString(curr_user_id==MrContact.MR_CONTACT_ID_SELF? R.string.LeaveGroup : R.string.RemoveMember)};
                        builder.setItems(items, new DialogInterface.OnClickListener() {
                            @Override
                            public void onClick(DialogInterface dialogInterface, int i) {

                                AlertDialog.Builder builder = new AlertDialog.Builder(getParentActivity());
                                builder.setPositiveButton(R.string.OK, new DialogInterface.OnClickListener() {
                                    @Override
                                    public void onClick(DialogInterface dialogInterface, int i) {
                                        MrMailbox.removeContactFromChat(chat_id, curr_user_id);
                                        NotificationCenter.getInstance().postNotificationName(NotificationCenter.updateInterfaces, MrMailbox.UPDATE_MASK_CHAT_MEMBERS);
                                    }
                                });
                                builder.setNegativeButton(R.string.Cancel, null);
                                String msg;
                                if( curr_user_id==MrContact.MR_CONTACT_ID_SELF ) {
                                    msg = context.getString(R.string.AskLeaveGroup);
                                }
                                else {
                                    msg = String.format(context.getString(R.string.AskRemoveMemberFromGroup), MrMailbox.getContact(curr_user_id).getDisplayName());
                                }
                                builder.setMessage(AndroidUtilities.replaceTags(msg));
                                showDialog(builder.create());
                            }
                        });
                        showDialog(builder.create());

                    }
                    return true;
                }
                else
                {
                    return false;
                }
            }
        });

        topView = new TopView(context);
        topView.setBackgroundColor(Theme.ACTION_BAR_COLOR);
        frameLayout.addView(topView);

        frameLayout.addView(actionBar);

        avatarImage = new BackupImageView(context);
        avatarImage.setRoundRadius(AndroidUtilities.dp(21));
        avatarImage.setPivotX(0);
        avatarImage.setPivotY(0);
        frameLayout.addView(avatarImage, LayoutHelper.createFrame(42, 42, Gravity.TOP | Gravity.START, 64-ANIM_OFF, 0, 0, 0));
        avatarImage.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (user_id==0 && chat_id != 0 ) {
                    // show menu to change the group image
                    AlertDialog.Builder builder = new AlertDialog.Builder(getParentActivity());
                    CharSequence[] items;
                    boolean hasPhoto = MrMailbox.getChat(chat_id).getProfileImage()!=null;
                    if ( !hasPhoto ) {
                        items = new CharSequence[]{context.getString(R.string.FromCamera), context.getString(R.string.FromGalley)};
                    } else {
                        items = new CharSequence[]{context.getString(R.string.FromCamera), context.getString(R.string.FromGalley), context.getString(R.string.Delete)};
                    }

                    builder.setTitle(R.string.EditImage);
                    builder.setItems(items, new DialogInterface.OnClickListener() {
                        @Override
                        public void onClick(DialogInterface dialogInterface, int i) {
                            if (i == 0) {
                                avatarUpdater.openCamera(); // results in a call to didUploadedPhoto()
                            } else if (i == 1) {
                                avatarUpdater.openGallery(); // results in a call to didUploadedPhoto()
                            } else if (i == 2) {
                                AlertDialog.Builder builder = new AlertDialog.Builder(getParentActivity());
                                builder.setMessage(context.getString(R.string.AskDeleteGroupImage));
                                builder.setPositiveButton(R.string.OK, new DialogInterface.OnClickListener() {
                                    @Override
                                    public void onClick(DialogInterface dialogInterface, int i) {
                                        if( MrMailbox.setChatProfileImage(chat_id, null)!=0 ) {
                                            AndroidUtilities.showDoneHint(getParentActivity());
                                        }
                                    }
                                });
                                builder.setNegativeButton(R.string.Cancel, null);
                                showDialog(builder.create());


                            }
                        }
                    });
                    showDialog(builder.create());
                }
            }
        });

        nameTextView = new SimpleTextView(context);
        nameTextView.setTextColor(Theme.ACTION_BAR_TITLE_COLOR);
        nameTextView.setTextSize(18);
        nameTextView.setGravity(Gravity.START);
        nameTextView.setLeftDrawableTopPadding(-AndroidUtilities.dp(1.0f));
        nameTextView.setRightDrawableTopPadding(-AndroidUtilities.dp(1.0f));
        nameTextView.setPivotX(0);
        nameTextView.setPivotY(0);
        frameLayout.addView(nameTextView, LayoutHelper.createFrame(LayoutHelper.WRAP_CONTENT, LayoutHelper.WRAP_CONTENT, Gravity.START | Gravity.TOP, 118-ANIM_OFF, 0, 0, 0));

        subtitleTextView = new SimpleTextView(context);
        subtitleTextView.setTextColor(Theme.ACTION_BAR_SUBTITLE_COLOR);
        subtitleTextView.setTextSize(14);
        subtitleTextView.setGravity(Gravity.START);
        frameLayout.addView(subtitleTextView, LayoutHelper.createFrame(LayoutHelper.WRAP_CONTENT, LayoutHelper.WRAP_CONTENT, Gravity.START | Gravity.TOP, 118-ANIM_OFF, 0, 8, 0));

        needLayout();

        listView.setOnScrollListener(new RecyclerView.OnScrollListener() {
            @Override
            public void onScrolled(RecyclerView recyclerView, int dx, int dy) {
                checkListViewScroll();
            }
        });

        return fragmentView;
    }

    @Override
    public void saveSelfArgs(Bundle args) {
        if (chat_id != 0) {
            if (avatarUpdater != null && avatarUpdater.currentPicturePath != null) {
                args.putString("path", avatarUpdater.currentPicturePath);
            }
        }
    }

    @Override
    public void restoreSelfArgs(Bundle args) {
        if (chat_id != 0) {
            //MessagesController.getInstance().loadChatInfo(chat_id, null, false);
            if (avatarUpdater != null) {
                avatarUpdater.currentPicturePath = args.getString("path");
            }
        }
    }

    @Override
    public void onActivityResultFragment(int requestCode, int resultCode, Intent data) {
        if (chat_id != 0) {
            avatarUpdater.onActivityResult(requestCode, resultCode, data);
        }
    }

    private void checkListViewScroll() {
        if (listView.getChildCount() <= 0) {
            return;
        }

        View child = listView.getChildAt(0);
        ListAdapter.Holder holder = (ListAdapter.Holder) listView.findContainingViewHolder(child);
        int top = child.getTop();
        int newOffset = 0;
        if (top >= 0 && holder != null && holder.getAdapterPosition() == 0) {
            newOffset = top;
        }
        if (extraHeight != newOffset) {
            extraHeight = newOffset;
            topView.invalidate();
            needLayout();
        }
    }

    private void needLayout() {
        FrameLayout.LayoutParams layoutParams;
        int newTop = (actionBar.getOccupyStatusBar() ? AndroidUtilities.statusBarHeight : 0) + ActionBar.getCurrentActionBarHeight();
        if (listView != null) {
            layoutParams = (FrameLayout.LayoutParams) listView.getLayoutParams();
            if (layoutParams.topMargin != newTop) {
                layoutParams.topMargin = newTop;
                listView.setLayoutParams(layoutParams);
            }
        }

        if (avatarImage != null) {
            float diff = extraHeight / (float) AndroidUtilities.dp(88);
            listView.setTopGlowOffset(extraHeight);

            float avatarY = (actionBar.getOccupyStatusBar() ? AndroidUtilities.statusBarHeight : 0)
                    + ActionBar.getCurrentActionBarHeight() / 2.0f * (1.0f + diff)
                    - 21 * AndroidUtilities.density + 12 * AndroidUtilities.density * diff;
            avatarImage.setScaleX((42 + 40 * diff) / 42.0f); // diff is 0 when atop, so "offset + x*0 / offset" is 1
            avatarImage.setScaleY((42 + 40 * diff) / 42.0f);
            avatarImage.setTranslationX(-AndroidUtilities.dp(42) * diff);
            avatarImage.setTranslationY((float) Math.ceil(avatarY));

            nameTextView.setTranslationX(/*-21 * AndroidUtilities.density * diff*/1);
            nameTextView.setTranslationY((float) Math.floor(avatarY) + AndroidUtilities.dp(1.3f) + AndroidUtilities.dp(14) * diff);
            subtitleTextView.setTranslationX(/*-21 * AndroidUtilities.density * diff*/1);
            subtitleTextView.setTranslationY((float) Math.floor(avatarY) + AndroidUtilities.dp(24) + (float) Math.floor(25 * AndroidUtilities.density) * diff);
            nameTextView.setScaleX(1.0f + 0.4f * diff);
            nameTextView.setScaleY(1.0f + 0.4f * diff);
            int width;
            width = AndroidUtilities.displaySize.x;
            width = (int) (width - AndroidUtilities.dp(118 + 8 + 40 * (1.0f - diff)) - nameTextView.getTranslationX());
            float width2 = nameTextView.getPaint().measureText(nameTextView.getText().toString()) * nameTextView.getScaleX() + nameTextView.getSideDrawablesSize();
            layoutParams = (FrameLayout.LayoutParams) nameTextView.getLayoutParams();
            if (width < width2) {
                layoutParams.width = (int) Math.ceil(width / nameTextView.getScaleX());
            } else {
                layoutParams.width = LayoutHelper.WRAP_CONTENT;
            }
            nameTextView.setLayoutParams(layoutParams);

            layoutParams = (FrameLayout.LayoutParams) subtitleTextView.getLayoutParams();
            layoutParams.rightMargin = (int) Math.ceil(subtitleTextView.getTranslationX() + AndroidUtilities.dp(8) + AndroidUtilities.dp(40) * (1.0f - diff));
            subtitleTextView.setLayoutParams(layoutParams);
        }
    }

    private void fixLayout() {
        if (fragmentView == null) {
            return;
        }
        fragmentView.getViewTreeObserver().addOnPreDrawListener(new ViewTreeObserver.OnPreDrawListener() {
            @Override
            public boolean onPreDraw() {
                if (fragmentView != null) {
                    checkListViewScroll();
                    needLayout();
                    fragmentView.getViewTreeObserver().removeOnPreDrawListener(this);
                }
                return true;
            }
        });
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        fixLayout();
    }

    @SuppressWarnings("unchecked")
    @Override
    public void didReceivedNotification(int id, final Object... args) {
        boolean reloadChatlist = false, updateList = false;

        if (id == NotificationCenter.updateInterfaces) {
            reloadChatlist = true;
            int mask = (Integer) args[0];
            if (user_id != 0) {
                if ((mask & MrMailbox.UPDATE_MASK_AVATAR) != 0 || (mask & MrMailbox.UPDATE_MASK_NAME) != 0 || (mask & MrMailbox.UPDATE_MASK_STATUS) != 0) {
                    updateProfileData();
                }
            } else if (chat_id != 0) {
                memberlistUserIds = MrMailbox.getChatContacts(chat_id);
                updateRowsIds();
                updateProfileData();
                listAdapter.notifyDataSetChanged();

                if ((mask & MrMailbox.UPDATE_MASK_AVATAR) != 0 || (mask & MrMailbox.UPDATE_MASK_NAME) != 0 || (mask & MrMailbox.UPDATE_MASK_STATUS) != 0) {
                    updateList = true;
                }
            }
        }
        else if( id == NotificationCenter.dialogsNeedReload||id==NotificationCenter.notificationsSettingsUpdated||id==NotificationCenter.messageSendError ) {
            reloadChatlist = true;
        }
        else if (id == NotificationCenter.contactsDidLoaded) {

        } else if (id == NotificationCenter.closeChats) {
            removeSelfFromStack();
        }

        if( reloadChatlist && user_id != 0 ) {
            chatlist = MrMailbox.getChatlist(0, null, user_id);
            updateRowsIds();
            listAdapter.notifyDataSetChanged();
            updateList = true;
        }

        if( updateList && listView != null ) {
            int count = listView.getChildCount();
            for (int a = 0; a < count; a++) {
                View child = listView.getChildAt(a);
                if (child instanceof UserCell) {
                    ((UserCell) child).update();
                }
            }
        }
    }

    @Override
    public void onResume() {
        super.onResume();
        if (listAdapter != null) {
            listAdapter.notifyDataSetChanged();
        }
        updateProfileData();
        fixLayout();
    }

    @Override
    protected void onTransitionAnimationStart(boolean isOpen, boolean backward) {
        NotificationCenter.getInstance().setAllowedNotificationsDutingAnimation(new int[]{NotificationCenter.dialogsNeedReload, NotificationCenter.closeChats});
        NotificationCenter.getInstance().setAnimationInProgress(true);
    }

    @Override
    protected void onTransitionAnimationEnd(boolean isOpen, boolean backward) {
        NotificationCenter.getInstance().setAnimationInProgress(false);
    }

    @Override
    public void updatePhotoAtIndex(int index) {

    }

    @Override
    public PhotoViewer.PlaceProviderObject getPlaceForPhoto(MessageObject messageObject, TLRPC.FileLocation fileLocation, int index) {
        if (fileLocation == null) {
            return null;
        }

        TLRPC.FileLocation photoBig = null;
        if (user_id != 0) {
            TLRPC.User user = MrMailbox.getUser(user_id);
            if (user != null && user.photo != null && user.photo.photo_big != null) {
                photoBig = user.photo.photo_big;
            }
        } else if (chat_id != 0) {
            TLRPC.Chat chat = MrChat.chatId2chat(chat_id);
            if (chat != null && chat.photo != null && chat.photo.photo_big != null) {
                photoBig = chat.photo.photo_big;
            }
        }


        if (photoBig != null && photoBig.local_id == fileLocation.local_id && photoBig.volume_id == fileLocation.volume_id && photoBig.dc_id == fileLocation.dc_id) {
            int coords[] = new int[2];
            avatarImage.getLocationInWindow(coords);
            PhotoViewer.PlaceProviderObject object = new PhotoViewer.PlaceProviderObject();
            object.viewX = coords[0];
            object.viewY = coords[1] - AndroidUtilities.statusBarHeight;
            object.parentView = avatarImage;
            object.imageReceiver = avatarImage.getImageReceiver();
            if (user_id != 0) {
                object.dialogId = user_id;
            } else if (chat_id != 0) {
                object.dialogId = -chat_id;
            }
            object.thumb = object.imageReceiver.getBitmap();
            object.size = -1;
            object.radius = avatarImage.getImageReceiver().getRoundRadius();
            object.scale = avatarImage.getScaleX();
            return object;
        }
        return null;
    }

    @Override
    public Bitmap getThumbForPhoto(MessageObject messageObject, TLRPC.FileLocation fileLocation, int index) {
        return null;
    }

    @Override
    public void willSwitchFromPhoto(MessageObject messageObject, TLRPC.FileLocation fileLocation, int index) { }

    @Override
    public void willHidePhotoViewer() {
        avatarImage.getImageReceiver().setVisible(true, true);
    }

    @Override
    public boolean isPhotoChecked(int index) { return false; }

    @Override
    public void setPhotoChecked(int index) { }

    @Override
    public boolean cancelButtonPressed() { return true; }

    @Override
    public void sendButtonPressed(int index) { }

    @Override
    public int getSelectedCount() { return 0; }

    public boolean isChat() {
        return chat_id != 0;
    }

    private void updateRowsIds() {

        rowCount = 0;

        changeNameRow = -1;
        settingsRow = -1;
        settingsShadowRow = -1;

        emailHeaderRow = -1;
        emailRow = -1;
        infoRow = -1;
        listShadowRow = -1;

        memberlistHeaderRow = -1;
        memberlistFirstRow = -1;
        memberlistLastRow = -1;
        addMemberRow = -1;
        showQrRow = -1;

        chatlistHeaderRow = -1;
        chatlistFirstRow = -1;
        chatlistLastRow = -1;
        startChatRow = -1;

        otherShadowRow = -1;
        otherHeaderRow = -1;
        addShortcutRow = -1;
        blockContactRow = -1;

        if (user_id != 0) {
            if( rowCount >= 1 ) { settingsShadowRow = rowCount++; }
            emailHeaderRow = rowCount++;
            emailRow = rowCount++;
            infoRow = rowCount++;
        }

        if( chat_id != 0 )
        {
            // chat profile
            if( rowCount >= 1 ) { listShadowRow = rowCount++; }
            memberlistHeaderRow = rowCount++;

            if( chat_id!=MrChat.MR_CHAT_ID_DEADDROP ) {
                addMemberRow = rowCount++;
            }

            if( MrMailbox.getChat(chat_id).isVerified() ) {
                showQrRow = rowCount++;
            }

            memberlistFirstRow = rowCount;
            rowCount += memberlistUserIds.length;
            memberlistLastRow = rowCount-1;
        }
        else if( user_id != 0 )
        {
            // user profile
            if( rowCount >= 1 ) { listShadowRow = rowCount++; }
            chatlistHeaderRow = rowCount++;

            startChatRow = rowCount++;

            chatlistFirstRow = rowCount;
            rowCount += chatlist.getCnt();
            chatlistLastRow = rowCount-1;
        }

        boolean addHeader = true;

        if( (user_id!=0 && user_id!=MrContact.MR_CONTACT_ID_SELF) || (chat_id!=0 && chat_id!=MrChat.MR_CHAT_ID_DEADDROP)) {
            if(addHeader) {otherShadowRow = rowCount++; otherHeaderRow = rowCount++; addHeader=false; }
            changeNameRow = rowCount++;
        }

        if( (chat_id!=0)
         || MrMailbox.getChatIdByContactId(user_id)!=0 ) { // no settings if there is no chat yet
            if(addHeader) {otherShadowRow = rowCount++; otherHeaderRow = rowCount++; addHeader=false; }
            settingsRow = rowCount++;
        }

        if( chat_id!=0 || MrMailbox.getChatIdByContactId(user_id)!=0 ) {
            if(addHeader) {otherShadowRow = rowCount++; otherHeaderRow = rowCount++; addHeader=false; }
            addShortcutRow = rowCount++;
        }

        if( user_id != 0 && user_id != MrContact.MR_CONTACT_ID_SELF ) {
            if(addHeader) {otherShadowRow = rowCount++; otherHeaderRow = rowCount++; addHeader=false; }
            blockContactRow = rowCount++;
        }
    }

    private boolean userBlocked()
    {
        boolean blocked = false;
        if( user_id!=0 ) {
            MrContact mrContact = MrMailbox.getContact(user_id);
            blocked = mrContact.isBlocked();
        }
        return blocked;
    }

    private void updateProfileData() {
        if (avatarImage == null || nameTextView == null) {
            return;
        }

        String newString;
        String newString2;

        MrContact mrContact = null;
        MrChat mrChat = null;
        if( user_id!=0 ) {
            mrContact = MrMailbox.getContact(user_id);
            newString = mrContact.getDisplayName();
            if( mrContact.isVerified() ) {
                newString2 = ApplicationLoader.applicationContext.getString(R.string.VerifiedContact);
            }
            else {
                newString2 = ApplicationLoader.applicationContext.getString(R.string.Contact);
            }
        }
        else {
            mrChat = MrMailbox.getChat(chat_id);
            newString = mrChat.getName();
            if( mrChat.isVerified() ) {
                newString2 = ApplicationLoader.applicationContext.getString(R.string.VerifiedGroup);
            }
            else {
                newString2 = ApplicationLoader.applicationContext.getString(R.string.Group);
            }
        }

        if (!nameTextView.getText().equals(newString)) {
            nameTextView.setText(newString);
        }

        if( (mrChat!=null? mrChat.isVerified() : mrContact.isVerified()) ) {
            nameTextView.setRightDrawable(R.drawable.verified);
        }

        if (!subtitleTextView.getText().equals(newString2)) {
            subtitleTextView.setText(newString2);
        }

        ContactsController.setupAvatar(avatarImage, avatarImage.imageReceiver, avatarDrawable, mrContact, mrChat);
    }

    @Override
    protected void onDialogDismiss(Dialog dialog) {
        if (listView != null) {
            listView.invalidateViews();
        }
    }

    private class ListAdapter extends RecyclerListView.Adapter {
        private Context mContext;

        private class Holder extends RecyclerView.ViewHolder {

            public Holder(View itemView) {
                super(itemView);
            }
        }

        public ListAdapter(Context context) {
            mContext = context;
        }

        @Override
        public RecyclerView.ViewHolder onCreateViewHolder(ViewGroup parent, int viewType) {
            View view = null;
            switch (viewType) {
                case ROWTYPE_TEXT_SETTINGS:
                    view = new TextSettingsCell(mContext);
                    break;

                case ROWTYPE_CONTACT:
                    view = new UserCell(mContext, 0);
                    break;

                case ROWTYPE_CHAT:
                    view = new ChatlistCell(mContext);
                    break;

                case ROWTYPE_SHADOW:
                    view = new ShadowSectionCell(mContext);
                    break;

                case ROWTYPE_INFO:
                    view = new TextInfoCell(mContext);
                    break;

                case ROWTYPE_HEADER:
                    view = new HeaderCell(mContext);
                    view.setBackgroundColor(0xffffffff);
                    break;
            }
            view.setLayoutParams(new RecyclerView.LayoutParams(RecyclerView.LayoutParams.MATCH_PARENT, RecyclerView.LayoutParams.WRAP_CONTENT));
            return new Holder(view);
        }

        @Override
        public void onBindViewHolder(RecyclerView.ViewHolder holder, int i) {
            boolean checkBackground = true;
            switch (holder.getItemViewType()) {
                case ROWTYPE_TEXT_SETTINGS:
                    TextSettingsCell textCell = (TextSettingsCell) holder.itemView;
                    textCell.setTextColor(0xff212121);
                    if (i == changeNameRow) {
                        textCell.setText(mContext.getString(R.string.EditName), true);
                    } else if (i == startChatRow) {
                        textCell.setText(mContext.getString(R.string.NewChat), false);
                    } else if (i == settingsRow) {
                        textCell.setText(mContext.getString(R.string.NotificationsAndSounds), true);
                    } else if (i == addMemberRow) {
                        textCell.setText(mContext.getString(R.string.AddMember), showQrRow!=-1);
                    }
                    else if( i == showQrRow ) {
                        textCell.setText(mContext.getString(R.string.QrShowInviteCode), false);
                    }
                    else if( i==emailRow ) {
                        textCell.setText(MrMailbox.getContact(user_id).getAddr(), true);
                    }
                    else if( i==infoRow ) {
                        textCell.setText(mContext.getString(R.string.Encryption), false);
                    }
                    else if( i == addShortcutRow ) {
                        textCell.setText(mContext.getString(R.string.AddShortcut), true);
                    }
                    else if( i == blockContactRow ) {
                        textCell.setText(userBlocked()? mContext.getString(R.string.UnblockContact) : mContext.getString(R.string.BlockContact), true);
                    }
                    break;

                case ROWTYPE_CONTACT:
                    UserCell userCell = ((UserCell) holder.itemView);
                    int curr_user_index = i - memberlistFirstRow;
                    if(curr_user_index>=0 && curr_user_index< memberlistUserIds.length) {
                        int curr_user_id = memberlistUserIds[curr_user_index];
                        MrContact mrContact = MrMailbox.getContact(curr_user_id);
                            userCell.setData(mrContact);
                    }
                    break;

                case ROWTYPE_CHAT:
                    ChatlistCell chatlistCell = ((ChatlistCell) holder.itemView);
                    int curr_chatlist_index = i - chatlistFirstRow;
                    if(curr_chatlist_index>=0 && curr_chatlist_index< chatlist.getCnt()) {
                        chatlistCell.useSeparator = (curr_chatlist_index != chatlist.getCnt() - 1);
                        MrChat mrChat = chatlist.getChatByIndex(curr_chatlist_index);
                        MrLot mrSummary = chatlist.getSummaryByIndex(curr_chatlist_index, mrChat);

                        chatlistCell.setChat(mrChat, mrSummary, -1,
                                true /*always show unread count*/);
                    }
                    break;

                case ROWTYPE_INFO:
                    TextInfoCell infoCell = ((TextInfoCell) holder.itemView);
                    infoCell.setText("foo");
                    infoCell.setBackgroundResource(R.drawable.greydivider);
                    checkBackground = false;
                    break;

                case ROWTYPE_HEADER:
                    HeaderCell headerCell = ((HeaderCell) holder.itemView);
                    if( i ==emailHeaderRow ) {
                        headerCell.setText(mContext.getString(R.string.EmailAddress));
                    }
                    else if( i ==chatlistHeaderRow ) {
                        headerCell.setText(mContext.getString(R.string.SharedChats));
                    }
                    else if( i==memberlistHeaderRow) {
                        headerCell.setText(MrMailbox.getChat(chat_id).getSubtitle());
                    }
                    else if( i ==otherHeaderRow ) {
                        headerCell.setText(mContext.getString(R.string.Settings));
                    }
                    break;

                default:
                    checkBackground = false;
                    break;
            }

            if (checkBackground) {
                boolean enabled =
                           i == settingsRow
                        || i == changeNameRow
                        || i == emailRow
                        || i == infoRow
                        || i == startChatRow
                        || i == addMemberRow
                        || i == showQrRow
                        || (i >= chatlistFirstRow && i <= chatlistLastRow)
                        || (i >= memberlistFirstRow && i <= memberlistLastRow);

                if (enabled) {
                    if (holder.itemView.getBackground() == null) {
                        holder.itemView.setBackgroundResource(R.drawable.list_selector);
                    }
                } else {
                    if (holder.itemView.getBackground() != null) {
                        holder.itemView.setBackgroundDrawable(null);
                    }
                }
            }
        }

        @Override
        public int getItemCount() {
            return rowCount;
        }

        @Override
        public int getItemViewType(int i) {
            if ( i == changeNameRow || i==startChatRow || i == settingsRow || i == addMemberRow || i == showQrRow || i==emailRow || i==infoRow
                    || i==addShortcutRow || i==blockContactRow ) {
                return ROWTYPE_TEXT_SETTINGS;
            } else if (i >= memberlistFirstRow && i <= memberlistLastRow) {
                return ROWTYPE_CONTACT;
            } else if (i >= chatlistFirstRow && i <= chatlistLastRow) {
                return ROWTYPE_CHAT;
            } else if(i== listShadowRow || i==settingsShadowRow || i==otherShadowRow ) {
                return ROWTYPE_SHADOW;
            }
            else if(i==chatlistHeaderRow || i==memberlistHeaderRow || i==emailHeaderRow|| i==otherHeaderRow ) {
                return ROWTYPE_HEADER;
            }
            return 0;
        }
    }
}
