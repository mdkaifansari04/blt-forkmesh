import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:forkmesh/models/models.dart';
import 'package:forkmesh/screens/repo_detail_screen.dart';
import 'package:forkmesh/services/api_service.dart';
import 'package:forkmesh/services/auth_service.dart';
import 'package:forkmesh/services/identity.dart';
import 'package:forkmesh/services/inbox_service.dart';
import 'package:forkmesh/services/settings_service.dart';
import 'package:forkmesh/theme.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

class CollaborationApiService extends ApiService {
  CollaborationApiService(super.settings);

  @override
  Future<List<RepoBranch>> branches(String owner, String name) async => [
    RepoBranch(name: 'main', isDefault: true),
  ];

  @override
  Future<RepoTree> tree(
    String owner,
    String name, {
    String path = '',
    String ref = '',
  }) async => RepoTree(path: path, entries: const []);

  @override
  Future<List<Issue>> publishedIssues(String owner, String name) async => [
    Issue(
      number: 12,
      title: 'Stabilize signed inbox',
      status: 'open',
      body: 'Mobile should make signed collaboration clear.',
      author: 'alice',
      labels: const ['mobile', 'signed'],
      milestone: 'v2 mobile',
      priority: 3,
      assignees: const ['mona', 'kai'],
      votes: 2,
      bountyUsd: 150,
      events: [
        IssueEvent(
          type: 'comment',
          body: 'I can reproduce this on mobile.',
          authorName: 'Mona',
        ),
      ],
    ),
  ];

  @override
  Future<List<RepoDiscussion>> publishedDiscussions(
    String owner,
    String name,
  ) async => [
    RepoDiscussion(
      number: 3,
      title: 'Mobile maintainer workflow',
      body: 'How should maintainers review from phones?',
      author: 'alice',
      category: 'ideas',
      events: [
        DiscussionEvent(
          type: 'comment',
          body: 'Keep desktop node as the canonical apply surface.',
          authorName: 'Mona',
        ),
      ],
    ),
  ];

  @override
  Future<List<Map<String, dynamic>>> commits(String owner, String name) async =>
      [
        {
          'hash': 'abcdef1234567890',
          'author': 'Mona',
          'date': '2026-07-04',
          'subject': 'Wire signed commit comments',
        },
      ];

  @override
  Future<RepoCommitDetail> commitDetail(
    String owner,
    String name,
    String hash, {
    Map<String, dynamic> fallbackCommit = const {},
  }) async => RepoCommitDetail(
    commit: {
      ...fallbackCommit,
      'hash': hash,
      'subject': 'Wire signed commit comments',
      'author': 'Mona',
    },
    files: const [],
    diff: '',
  );

  @override
  Future<List<PublishedPull>> publishedPulls(String owner, String name) async =>
      [
        PublishedPull(
          number: 7,
          title: 'Add signed review flow',
          status: 'open',
          body: 'Adds mobile review controls.',
          base: 'main',
          head: 'review/mobile',
          patch: 'diff --git a/lib/a.dart b/lib/a.dart\n+review',
          signed: true,
          events: const [
            PullEvent(
              type: 'comment',
              body: 'Review changes before merge.',
              authorName: 'Kai',
            ),
            PullEvent(
              type: 'review',
              state: 'approve',
              body: 'LGTM from mobile.',
              authorName: 'Mona',
            ),
            PullEvent(
              type: 'review',
              state: 'changes_requested',
              body: 'Please add a regression test.',
              authorName: 'Lin',
            ),
          ],
        ),
      ];
}

Future<void> _pumpRepo(
  WidgetTester tester,
  ApiService api, {
  InboxService? inbox,
  Identity? identity,
  AuthService? auth,
}) async {
  tester.view.physicalSize = const Size(900, 1200);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.resetPhysicalSize);
  addTearDown(tester.view.resetDevicePixelRatio);
  final repo = Repository(owner: 'owner', name: 'repo', defaultBranch: 'main');
  await tester.pumpWidget(
    MultiProvider(
      providers: [
        Provider<ApiService>.value(value: api),
        if (identity != null) Provider<Identity>.value(value: identity),
        if (auth != null)
          ChangeNotifierProvider<AuthService>.value(value: auth),
        if (inbox != null) Provider<InboxService>.value(value: inbox),
      ],
      child: MaterialApp(
        theme: buildForkMeshLightTheme(),
        home: RepoDetailScreen(repo: repo),
      ),
    ),
  );
  await tester.pumpAndSettle();
}

class RecordingInboxService extends InboxService {
  RecordingInboxService(super.settings, super.identity);

  String title = '';
  String body = '';
  List<String> labels = const [];
  String milestone = '';
  int priority = 0;
  List<String> assignees = const [];
  int reviewNumber = 0;
  String reviewState = '';
  String reviewBody = '';
  int reviewCount = 0;
  String discussionTitle = '';
  String discussionBody = '';
  String discussionCategory = '';
  int discussionCommentNumber = 0;
  String discussionCommentBody = '';
  String commitSha = '';
  String commitBody = '';
  String subscriptionNode = '';
  String subscriptionSource = '';
  int subscriptionNumber = 0;
  bool? subscriptionSubscribed;
  int subscriptionCount = 0;

  @override
  Future<void> submitNewIssue(
    String owner,
    String name, {
    required String title,
    required String body,
    List<String> labels = const [],
    String milestone = '',
    int priority = 0,
    List<String> assignees = const [],
  }) async {
    this.title = title;
    this.body = body;
    this.labels = labels;
    this.milestone = milestone;
    this.priority = priority;
    this.assignees = assignees;
  }

  @override
  Future<void> reviewPull(
    String owner,
    String name,
    int number,
    String state,
    String body,
  ) async {
    reviewNumber = number;
    reviewState = state;
    reviewBody = body;
    reviewCount += 1;
  }

  @override
  Future<void> submitNewDiscussion(
    String owner,
    String name, {
    required String title,
    required String body,
    String category = 'general',
  }) async {
    discussionTitle = title;
    discussionBody = body;
    discussionCategory = category;
  }

  @override
  Future<void> commentOnDiscussion(
    String owner,
    String name,
    int number,
    String body,
  ) async {
    discussionCommentNumber = number;
    discussionCommentBody = body;
  }

  @override
  Future<void> commentOnCommit(
    String owner,
    String name,
    String sha,
    String body,
  ) async {
    commitSha = sha;
    commitBody = body;
  }

  @override
  Future<void> setThreadSubscription(
    String owner,
    String name, {
    required String node,
    required String source,
    required int number,
    required bool subscribed,
  }) async {
    subscriptionNode = node;
    subscriptionSource = source;
    subscriptionNumber = number;
    subscriptionSubscribed = subscribed;
    subscriptionCount += 1;
  }
}

void main() {
  testWidgets('issue detail can subscribe and unsubscribe from thread alerts', (
    tester,
  ) async {
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final identity = await Identity.loadOrCreate();
    final auth = await AuthService.create(settings, identity);
    await auth.authenticatePreview();
    final inbox = RecordingInboxService(settings, identity);
    await _pumpRepo(
      tester,
      CollaborationApiService(settings),
      inbox: inbox,
      identity: identity,
      auth: auth,
    );

    await tester.tap(find.text('Issues'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Stabilize signed inbox'));
    await tester.pumpAndSettle();

    expect(find.text('Subscribe thread'), findsOneWidget);
    await tester.tap(find.text('Subscribe thread'));
    await tester.pumpAndSettle();

    expect(inbox.subscriptionNode, 'preview-node');
    expect(inbox.subscriptionSource, 'issue');
    expect(inbox.subscriptionNumber, 12);
    expect(inbox.subscriptionSubscribed, isTrue);
    expect(find.text('Unsubscribe thread'), findsOneWidget);

    await tester.tap(find.text('Unsubscribe thread'));
    await tester.pumpAndSettle();

    expect(inbox.subscriptionCount, 2);
    expect(inbox.subscriptionSubscribed, isFalse);
    expect(find.text('Subscribe thread'), findsOneWidget);
  });

  testWidgets('pull detail can subscribe and unsubscribe from thread alerts', (
    tester,
  ) async {
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final identity = await Identity.loadOrCreate();
    final auth = await AuthService.create(settings, identity);
    await auth.authenticatePreview();
    final inbox = RecordingInboxService(settings, identity);
    await _pumpRepo(
      tester,
      CollaborationApiService(settings),
      inbox: inbox,
      identity: identity,
      auth: auth,
    );

    await tester.tap(find.text('Pulls'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Add signed review flow'));
    await tester.pumpAndSettle();

    expect(find.text('Subscribe thread'), findsOneWidget);
    await tester.tap(find.text('Subscribe thread'));
    await tester.pumpAndSettle();

    expect(inbox.subscriptionNode, 'preview-node');
    expect(inbox.subscriptionSource, 'pull');
    expect(inbox.subscriptionNumber, 7);
    expect(inbox.subscriptionSubscribed, isTrue);
    expect(find.text('Unsubscribe thread'), findsOneWidget);

    await tester.tap(find.text('Unsubscribe thread'));
    await tester.pumpAndSettle();

    expect(inbox.subscriptionCount, 2);
    expect(inbox.subscriptionSubscribed, isFalse);
    expect(find.text('Subscribe thread'), findsOneWidget);
  });

  testWidgets('thread alert control is disabled without account signing key', (
    tester,
  ) async {
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    await _pumpRepo(tester, CollaborationApiService(settings));

    await tester.tap(find.text('Issues'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Stabilize signed inbox'));
    await tester.pumpAndSettle();

    expect(find.text('Thread alerts unavailable'), findsOneWidget);
    final button = tester.widget<OutlinedButton>(
      find.widgetWithText(OutlinedButton, 'Thread alerts unavailable'),
    );
    expect(button.onPressed, isNull);
  });

  testWidgets('issue detail surfaces signed collaboration state', (
    tester,
  ) async {
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    await _pumpRepo(tester, CollaborationApiService(settings));

    await tester.tap(find.text('Issues'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Stabilize signed inbox'));
    await tester.pumpAndSettle();

    expect(find.text('mobile'), findsOneWidget);
    expect(find.text('signed'), findsOneWidget);
    expect(find.text('2 votes'), findsOneWidget);
    expect(find.text(r'$150 bounty'), findsOneWidget);
    expect(find.text('priority 3'), findsOneWidget);
    expect(find.text('v2 mobile'), findsOneWidget);
    expect(find.text('mona'), findsOneWidget);
    expect(find.text('kai'), findsOneWidget);
    expect(find.text('I can reproduce this on mobile.'), findsOneWidget);
    expect(
      find.textContaining('pending the repo owner applying'),
      findsOneWidget,
    );
  });

  testWidgets('new issue dialog submits collaboration metadata', (
    tester,
  ) async {
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final identity = await Identity.loadOrCreate();
    final api = CollaborationApiService(settings);
    final inbox = RecordingInboxService(settings, identity);
    await _pumpRepo(tester, api, inbox: inbox);

    await tester.tap(find.text('Issues'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('New issue'));
    await tester.pumpAndSettle();

    await tester.enterText(
      find.bySemanticsLabel('Title'),
      'Polish mobile issues',
    );
    await tester.enterText(
      find.bySemanticsLabel('Description'),
      'Add the same metadata controls mobile maintainers expect.',
    );
    await tester.enterText(find.bySemanticsLabel('Labels'), 'mobile, signed');
    await tester.enterText(find.bySemanticsLabel('Priority'), '4');
    await tester.enterText(find.bySemanticsLabel('Milestone'), 'v2 mobile');
    await tester.enterText(find.bySemanticsLabel('Assignees'), 'mona, kai');
    await tester.tap(find.text('Submit'));
    await tester.pumpAndSettle();

    expect(inbox.title, 'Polish mobile issues');
    expect(
      inbox.body,
      'Add the same metadata controls mobile maintainers expect.',
    );
    expect(inbox.labels, ['mobile', 'signed']);
    expect(inbox.priority, 4);
    expect(inbox.milestone, 'v2 mobile');
    expect(inbox.assignees, ['mona', 'kai']);
    expect(
      find.textContaining('pending the repo owner applying'),
      findsOneWidget,
    );
  });

  testWidgets('new discussion composer submits selected category', (
    tester,
  ) async {
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final identity = await Identity.loadOrCreate();
    final auth = await AuthService.create(settings, identity);
    await auth.authenticatePreview();
    final inbox = RecordingInboxService(settings, identity);
    await _pumpRepo(
      tester,
      CollaborationApiService(settings),
      inbox: inbox,
      identity: identity,
      auth: auth,
    );

    await tester.tap(find.text('Discussions'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('New discussion'));
    await tester.pumpAndSettle();

    await tester.enterText(
      find.bySemanticsLabel('Title'),
      'Mobile review rituals',
    );
    await tester.tap(find.text('Ideas'));
    await tester.enterText(
      find.bySemanticsLabel('Body'),
      'Let maintainers review without pretending the phone applies Git changes.',
    );
    await tester.tap(find.text('Submit'));
    await tester.pumpAndSettle();

    expect(inbox.discussionTitle, 'Mobile review rituals');
    expect(inbox.discussionCategory, 'ideas');
    expect(
      inbox.discussionBody,
      'Let maintainers review without pretending the phone applies Git changes.',
    );
    expect(
      find.textContaining('pending the repo owner applying'),
      findsWidgets,
    );
  });

  testWidgets('discussion detail shows category and signed reply composer', (
    tester,
  ) async {
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final identity = await Identity.loadOrCreate();
    final auth = await AuthService.create(settings, identity);
    await auth.authenticatePreview();
    final inbox = RecordingInboxService(settings, identity);
    await _pumpRepo(
      tester,
      CollaborationApiService(settings),
      inbox: inbox,
      identity: identity,
      auth: auth,
    );

    await tester.tap(find.text('Discussions'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Mobile maintainer workflow'));
    await tester.pumpAndSettle();

    expect(find.text('ideas'), findsOneWidget);
    expect(
      find.text('Keep desktop node as the canonical apply surface.'),
      findsOneWidget,
    );
    expect(find.text('Signed discussion reply'), findsOneWidget);
    await tester.enterText(
      find.byWidgetPredicate(
        (widget) =>
            widget is TextField &&
            widget.decoration?.labelText == 'Write a signed reply',
      ),
      'Replies go through the discussion inbox.',
    );
    await tester.tap(find.text('Submit reply'));
    await tester.pumpAndSettle();

    expect(inbox.discussionCommentNumber, 3);
    expect(
      inbox.discussionCommentBody,
      'Replies go through the discussion inbox.',
    );
  });

  testWidgets('commit comment composer submits signed commit comment', (
    tester,
  ) async {
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final identity = await Identity.loadOrCreate();
    final auth = await AuthService.create(settings, identity);
    await auth.authenticatePreview();
    final inbox = RecordingInboxService(settings, identity);
    await _pumpRepo(
      tester,
      CollaborationApiService(settings),
      inbox: inbox,
      identity: identity,
      auth: auth,
    );

    await tester.tap(find.text('Commits'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Wire signed commit comments'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Comment on commit'));
    await tester.pumpAndSettle();

    expect(find.text('Signed commit comment'), findsOneWidget);
    expect(
      find.textContaining(
        'Commit comments are signed and sent to the commit inbox',
      ),
      findsOneWidget,
    );
    await tester.enterText(
      find.bySemanticsLabel('Comment'),
      'This commit is ready for mobile review.',
    );
    await tester.tap(find.text('Submit comment'));
    await tester.pumpAndSettle();

    expect(inbox.commitSha, 'abcdef1234567890');
    expect(inbox.commitBody, 'This commit is ready for mobile review.');
  });

  testWidgets('approve review composer submits approve state', (tester) async {
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final identity = await Identity.loadOrCreate();
    final auth = await AuthService.create(settings, identity);
    await auth.authenticatePreview();
    final inbox = RecordingInboxService(settings, identity);
    await _pumpRepo(
      tester,
      CollaborationApiService(settings),
      inbox: inbox,
      identity: identity,
      auth: auth,
    );

    await tester.tap(find.text('Pulls'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Add signed review flow'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Review / comment'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Approve').last);
    await tester.pumpAndSettle();

    expect(find.text('Approve PR #7'), findsOneWidget);
    expect(
      find.textContaining('Approval is signed and sent to the pull inbox'),
      findsOneWidget,
    );
    await tester.enterText(find.bySemanticsLabel('Review note'), 'Looks good.');
    await tester.tap(find.text('Submit review'));
    await tester.pumpAndSettle();

    expect(inbox.reviewNumber, 7);
    expect(inbox.reviewState, 'approve');
    expect(inbox.reviewBody, 'Looks good.');
    expect(inbox.reviewCount, 1);
    expect(
      find.textContaining('pending the repo owner applying'),
      findsWidgets,
    );
  });

  testWidgets('request changes composer requires a review note', (
    tester,
  ) async {
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    final identity = await Identity.loadOrCreate();
    final auth = await AuthService.create(settings, identity);
    await auth.authenticatePreview();
    final inbox = RecordingInboxService(settings, identity);
    await _pumpRepo(
      tester,
      CollaborationApiService(settings),
      inbox: inbox,
      identity: identity,
      auth: auth,
    );

    await tester.tap(find.text('Pulls'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Add signed review flow'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Review / comment'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Request changes'));
    await tester.pumpAndSettle();

    expect(find.text('Request changes on PR #7'), findsOneWidget);
    await tester.tap(find.text('Submit review'));
    await tester.pumpAndSettle();

    expect(inbox.reviewCount, 0);
    expect(find.text('A note is required to request changes.'), findsOneWidget);

    await tester.enterText(
      find.bySemanticsLabel('Review note'),
      'Please cover the empty state before owner apply.',
    );
    await tester.tap(find.text('Submit review'));
    await tester.pumpAndSettle();

    expect(inbox.reviewNumber, 7);
    expect(inbox.reviewState, 'request-changes');
    expect(
      inbox.reviewBody,
      'Please cover the empty state before owner apply.',
    );
    expect(inbox.reviewCount, 1);
  });

  testWidgets('pull detail renders signed comment and review timeline', (
    tester,
  ) async {
    SharedPreferences.setMockInitialValues({});
    final settings = await SettingsService.create();
    await _pumpRepo(tester, CollaborationApiService(settings));

    await tester.tap(find.text('Pulls'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Add signed review flow'));
    await tester.pumpAndSettle();

    expect(find.text('Timeline'), findsOneWidget);
    expect(find.text('Comment'), findsOneWidget);
    expect(find.text('Approved'), findsOneWidget);
    expect(find.text('Requested changes'), findsOneWidget);
    expect(find.text('Review changes before merge.'), findsOneWidget);
    expect(find.text('LGTM from mobile.'), findsOneWidget);
    expect(find.text('Please add a regression test.'), findsOneWidget);
    expect(
      find.textContaining('pending the repo owner applying'),
      findsWidgets,
    );
  });
}
