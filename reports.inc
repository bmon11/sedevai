// ===================== REPORTS (modules/reports: report.ts + reports.routes.ts) =====================
// Analytics ("Тайлан"): which stories and categories were hot, day by day, and what the runs cost.
// Every edition re-creates its topics, so a *story* links them: a topic continues a story when it was
// copied from the story's last topic (reusedFrom) or shares enough items with it (>= 2, or half of the
// smaller one). Only stories seen within the last 48 hours can continue. Score = metrics.hot (0-1),
// or 1 - position / topicCount for editions from before the hot score.

const
  REP_LINK_HOURS = 48;
  REP_MIN_SHARED = 2;
  REP_MIN_SHARED_RATIO = 0.5;
  REP_TOP_PER_DAY = 5;
  REP_TREND_COUNT = 8;
  REP_CATEGORIES: array[0..6] of string = ('politics', 'economy', 'society', 'world', 'sports', 'culture', 'tech');

type
  TRepPoint = record
    at: string;
    day: string;
    topicId: string;
    score: Double;
  end;

  TRepStory = record
    id, title, category, topicId, firstSeen, lastSeen: string;
    peak: Double;
    editions: Integer;
    items: TStringList;
    lastAt: TDateTime;
    points: array of TRepPoint;
  end;

  TRepTopic = record
    id, title, category, reusedFrom: string;
    position: Double;
    hasHot: Boolean;
    hot: Double;
    itemIds: TStringList;
  end;

  TRepEdition = record
    id: string;
    publishedAt: TDateTime;
    topics: array of TRepTopic;
  end;

var
  RepStories: array of TRepStory;

function Rep_Category(const c: string): string;
var
  i: Integer;
begin
  for i := 0 to High(REP_CATEGORIES) do
    if REP_CATEGORIES[i] = c then exit(c);
  Result := 'society';
end;

// Mongolia midnight of a day as UTC instant
function Rep_StartOf(const day: string): TDateTime;
begin
  Result := IncHour(ParseIso(day), -8);
end;

function Rep_Num(v: Double): TJSONData;
begin
  if (Frac(v) = 0) and (Abs(v) < 1E15) then Result := TJSONInt64Number.Create(Trunc(v))
  else Result := TJSONFloatNumber.Create(v);
end;

// links each topic to a story, in publish order. fills RepStories
procedure Rep_LinkStories(var eds: array of TRepEdition);
var
  order, tord, openIdx: array of Integer;
  claimed: array of Boolean;
  byTopic: TStringList;
  i, j, k, t, e, si, best, n, cnt, tmp: Integer;
  at: TDateTime;
  ed: TRepEdition;
  tp: TRepTopic;
  score, ratio, mn: Double;
  iso: string;
begin
  for i := 0 to High(RepStories) do RepStories[i].items.Free;
  SetLength(RepStories, 0);
  byTopic := TStringList.Create;
  // stable sort editions by publishedAt (insertion sort)
  SetLength(order, Length(eds));
  for i := 0 to High(eds) do order[i] := i;
  for i := 1 to High(order) do
  begin
    tmp := order[i]; j := i - 1;
    while (j >= 0) and (eds[order[j]].publishedAt > eds[tmp].publishedAt) do begin order[j + 1] := order[j]; Dec(j); end;
    order[j + 1] := tmp;
  end;
  for e := 0 to High(order) do
  begin
    ed := eds[order[e]];
    at := ed.publishedAt;
    iso := IsoOf(at);
    SetLength(openIdx, 0);
    for i := 0 to High(RepStories) do
      if (at - RepStories[i].lastAt) * 24 <= REP_LINK_HOURS + 1E-9 then
      begin
        SetLength(openIdx, Length(openIdx) + 1);
        openIdx[High(openIdx)] := i;
      end;
    SetLength(claimed, Length(RepStories));
    for i := 0 to High(claimed) do claimed[i] := False;
    cnt := Length(ed.topics);
    // stable sort topics by position
    SetLength(tord, cnt);
    for i := 0 to cnt - 1 do tord[i] := i;
    for i := 1 to cnt - 1 do
    begin
      tmp := tord[i]; j := i - 1;
      while (j >= 0) and (ed.topics[tord[j]].position > ed.topics[tmp].position) do begin tord[j + 1] := tord[j]; Dec(j); end;
      tord[j + 1] := tmp;
    end;
    for t := 0 to cnt - 1 do
    begin
      tp := ed.topics[tord[t]];
      if tp.hasHot then score := tp.hot
      else score := 1 - tp.position / Max(cnt, 1);
      si := -1;
      if tp.reusedFrom <> '' then
      begin
        k := byTopic.IndexOfName(tp.reusedFrom);
        if k >= 0 then si := StrToInt(byTopic.ValueFromIndex[k]);
      end;
      if (si < 0) or ((si < Length(claimed)) and claimed[si]) then
      begin
        si := -1;
        best := 0;
        for k := 0 to High(openIdx) do
        begin
          i := openIdx[k];
          if claimed[i] then continue;
          n := 0;
          for j := 0 to tp.itemIds.Count - 1 do
            if RepStories[i].items.IndexOf(tp.itemIds[j]) >= 0 then Inc(n);
          mn := Min(RepStories[i].items.Count, tp.itemIds.Count);
          ratio := n / Max(1, mn);
          if ((n >= REP_MIN_SHARED) or (ratio >= REP_MIN_SHARED_RATIO)) and (n > best) then
          begin
            best := n;
            si := i;
          end;
        end;
      end;
      if si < 0 then
      begin
        SetLength(RepStories, Length(RepStories) + 1);
        si := High(RepStories);
        RepStories[si].id := tp.id;
        RepStories[si].title := tp.title;
        RepStories[si].category := Rep_Category(tp.category);
        RepStories[si].topicId := tp.id;
        RepStories[si].firstSeen := iso;
        RepStories[si].lastSeen := iso;
        RepStories[si].peak := 0;
        RepStories[si].editions := 0;
        RepStories[si].items := TStringList.Create;
        RepStories[si].lastAt := at;
        SetLength(RepStories[si].points, 0);
        SetLength(claimed, Length(RepStories));
        claimed[si] := False;
      end;
      claimed[si] := True;
      k := byTopic.IndexOfName(tp.id);
      if k >= 0 then byTopic.Delete(k);
      byTopic.Add(tp.id + '=' + IntToStr(si));
      RepStories[si].title := tp.title;
      RepStories[si].category := Rep_Category(tp.category);
      RepStories[si].topicId := tp.id;
      RepStories[si].lastSeen := iso;
      RepStories[si].lastAt := at;
      if score > RepStories[si].peak then RepStories[si].peak := score;
      RepStories[si].editions := RepStories[si].editions + 1;
      RepStories[si].items.Clear;
      RepStories[si].items.AddStrings(tp.itemIds);
      SetLength(RepStories[si].points, Length(RepStories[si].points) + 1);
      RepStories[si].points[High(RepStories[si].points)].day := ToDay(ed.publishedAt);
      RepStories[si].points[High(RepStories[si].points)].score := score;
      RepStories[si].points[High(RepStories[si].points)].at := iso;
      RepStories[si].points[High(RepStories[si].points)].topicId := tp.id;
    end;
  end;
  byTopic.Free;
end;

procedure Routes_reports;
var
  count, i, j, k, d, ti, best, tmp, ne: Integer;
  qs, start, endDay, firstDay, rid, s: string;
  qv: Double;
  days, topicIdSet, distinct: TStringList;
  allEd, allTop, allRuns: TJSONArray;
  eds: array of TRepEdition;
  ed, tdoc, run: TJSONData;
  tids, arr: TJSONArray;
  lo, hi, pub: TDateTime;
  outObj, totals, catDaily, cd, so, costs, ctot, dd: TJSONObject;
  daysArr, cats, dailyTop, storiesArr, trendsArr, sts, scs, tops, pts, dailyArr: TJSONArray;
  inRange: array of Boolean;     // per story: has any point in range
  rank, rankStory: array of Integer;
  rankScore: array of Double;
  sc, w1, w2, peak: Double;
  editionCount, topicsTotal, storiesTotal, newStories: Integer;
  catStories, catScore: array of array of Double;
  sortIdx: array of Integer;
  weights: array of Double;
  peaks: array of Double;
  socialD, llmD: array of Double;
  tSocial, tLlm, tReq, tCred, tTok: Double;
  ptDay: string;
begin
  // GET /api/admin/reports?days=14
  if Route('GET', '/api/admin/reports') then
  begin
    RequirePerm('reports');
    // z.coerce.number().int().min(1).max(90).default(14)
    if Req.QueryFields.IndexOfName('days') < 0 then count := 14
    else
    begin
      qs := Trim(Req.QueryFields.Values['days']);
      if qs = '' then qv := 0
      else if not TryStrToFloat(qs, qv) then raise EApi.CreateS(400, '✖ Invalid input: expected number, received NaN' + #10 + '  → at days');
      if Frac(qv) <> 0 then raise EApi.CreateS(400, '✖ Invalid input: expected int, received number' + #10 + '  → at days');
      if qv < 1 then raise EApi.CreateS(400, '✖ Too small: expected number to be >=1' + #10 + '  → at days');
      if qv > 90 then raise EApi.CreateS(400, '✖ Too big: expected number to be <=90' + #10 + '  → at days');
      count := Trunc(qv);
    end;
    endDay := TodayMn;
    start := AddDays(endDay, -(count - 1));
    days := TStringList.Create;
    for i := 0 to count - 1 do days.Add(AddDays(start, i));
    firstDay := days[0];

    // editions: two extra days before the range so stories already running at its start are linked; mn only
    lo := Rep_StartOf(AddDays(start, -2));
    hi := Rep_StartOf(AddDays(endDay, 1));
    allEd := DbLoad('editions');
    allTop := DbLoad('topics');
    SetLength(eds, 0);
    for i := 0 to allEd.Count - 1 do
    begin
      ed := allEd.Items[i];
      if JStr(ed, 'region') = 'world' then continue;
      pub := ParseIso(JStr(ed, 'publishedAt'));
      if (pub < lo) or (pub >= hi) then continue;
      SetLength(eds, Length(eds) + 1);
      ne := High(eds);
      eds[ne].id := JStr(ed, '_id');
      eds[ne].publishedAt := pub;
      SetLength(eds[ne].topics, 0);
      tids := JArr(ed, 'topicIds');
      if tids <> nil then
        for j := 0 to tids.Count - 1 do
        begin
          rid := tids.Items[j].AsString;
          tdoc := nil;
          for k := 0 to allTop.Count - 1 do
            if JStr(allTop.Items[k], '_id') = rid then begin tdoc := allTop.Items[k]; break; end;
          if (tdoc = nil) or JBool(tdoc, 'hidden') then continue;
          SetLength(eds[ne].topics, Length(eds[ne].topics) + 1);
          with eds[ne].topics[High(eds[ne].topics)] do
          begin
            id := JStr(tdoc, '_id');
            title := JStr(tdoc, 'title');
            category := JStr(tdoc, 'category');
            position := JNum(tdoc, 'position');
            reusedFrom := JStr(tdoc, 'reusedFrom');
            hasHot := (JPath(tdoc, 'metrics.hot') <> nil) and (JPath(tdoc, 'metrics.hot').JSONType = jtNumber);
            if hasHot then hot := JPath(tdoc, 'metrics.hot').AsFloat else hot := 0;
            itemIds := TStringList.Create;
            arr := JArr(tdoc, 'articleIds');
            if arr <> nil then for k := 0 to arr.Count - 1 do itemIds.Add(arr.Items[k].AsString);
            arr := JArr(tdoc, 'postIds');
            if arr <> nil then for k := 0 to arr.Count - 1 do itemIds.Add(arr.Items[k].AsString);
          end;
        end;
    end;
    allEd.Free;
    allTop.Free;

    Rep_LinkStories(eds);

    // ---- buildReport ----
    // keep only points within the days
    SetLength(inRange, Length(RepStories));
    for i := 0 to High(RepStories) do
    begin
      k := 0;
      for j := 0 to High(RepStories[i].points) do
        if days.IndexOf(RepStories[i].points[j].day) >= 0 then
        begin
          RepStories[i].points[k] := RepStories[i].points[j];
          Inc(k);
        end;
      SetLength(RepStories[i].points, k);
      inRange[i] := k > 0;
    end;

    SetLength(catStories, Length(REP_CATEGORIES), count);
    SetLength(catScore, Length(REP_CATEGORIES), count);
    for i := 0 to High(REP_CATEGORIES) do
      for d := 0 to count - 1 do begin catStories[i][d] := 0; catScore[i][d] := 0; end;

    dailyTop := TJSONArray.Create;
    for d := 0 to count - 1 do
    begin
      SetLength(rankStory, 0); SetLength(rankScore, 0);
      for i := 0 to High(RepStories) do
      begin
        if not inRange[i] then continue;
        sc := -1;
        for j := 0 to High(RepStories[i].points) do
          if RepStories[i].points[j].day = days[d] then
            if RepStories[i].points[j].score > sc then sc := RepStories[i].points[j].score;
        if sc < 0 then continue;
        // insert sorted desc, stable
        SetLength(rankStory, Length(rankStory) + 1); SetLength(rankScore, Length(rankScore) + 1);
        k := High(rankStory);
        while (k > 0) and (rankScore[k - 1] < sc) do
        begin
          rankStory[k] := rankStory[k - 1]; rankScore[k] := rankScore[k - 1]; Dec(k);
        end;
        rankStory[k] := i; rankScore[k] := sc;
      end;
      for k := 0 to High(rankStory) do
        for j := 0 to High(REP_CATEGORIES) do
          if REP_CATEGORIES[j] = RepStories[rankStory[k]].category then
          begin
            catStories[j][d] := catStories[j][d] + 1;
            catScore[j][d] := catScore[j][d] + rankScore[k];
          end;
      tops := TJSONArray.Create;
      for k := 0 to Min(REP_TOP_PER_DAY, Length(rankStory)) - 1 do
        tops.Add(TJSONObject.Create(['id', RepStories[rankStory[k]].id, 'score', Rep_Num(rankScore[k])]));
      dailyTop.Add(TJSONObject.Create(['day', days[d], 'stories', tops]));
    end;

    // trends: stories with more than one point, by the sum of their scores
    SetLength(sortIdx, 0); SetLength(weights, 0);
    for i := 0 to High(RepStories) do
    begin
      if not inRange[i] or (Length(RepStories[i].points) <= 1) then continue;
      w1 := 0;
      for j := 0 to High(RepStories[i].points) do w1 := w1 + RepStories[i].points[j].score;
      SetLength(sortIdx, Length(sortIdx) + 1); SetLength(weights, Length(weights) + 1);
      k := High(sortIdx);
      while (k > 0) and (weights[k - 1] < w1) do
      begin
        sortIdx[k] := sortIdx[k - 1]; weights[k] := weights[k - 1]; Dec(k);
      end;
      sortIdx[k] := i; weights[k] := w1;
    end;
    trendsArr := TJSONArray.Create;
    for k := 0 to Min(REP_TREND_COUNT, Length(sortIdx)) - 1 do
    begin
      pts := TJSONArray.Create;
      for j := 0 to High(RepStories[sortIdx[k]].points) do
        pts.Add(TJSONObject.Create(['at', RepStories[sortIdx[k]].points[j].at, 'score', Rep_Num(RepStories[sortIdx[k]].points[j].score)]));
      trendsArr.Add(TJSONObject.Create(['id', RepStories[sortIdx[k]].id, 'points', pts]));
    end;

    // totals
    editionCount := 0;
    for i := 0 to High(eds) do
      if days.IndexOf(ToDay(eds[i].publishedAt)) >= 0 then Inc(editionCount);
    topicsTotal := 0; storiesTotal := 0; newStories := 0;
    for i := 0 to High(RepStories) do
      if inRange[i] then
      begin
        Inc(storiesTotal);
        topicsTotal := topicsTotal + Length(RepStories[i].points);
        if ToDay(ParseIso(RepStories[i].firstSeen)) >= firstDay then Inc(newStories);
      end;

    // stories by in-range peak desc (stable)
    SetLength(sortIdx, 0); SetLength(peaks, 0);
    for i := 0 to High(RepStories) do
    begin
      if not inRange[i] then continue;
      peak := -1E300;
      for j := 0 to High(RepStories[i].points) do
        if RepStories[i].points[j].score > peak then peak := RepStories[i].points[j].score;
      SetLength(sortIdx, Length(sortIdx) + 1); SetLength(peaks, Length(peaks) + 1);
      k := High(sortIdx);
      while (k > 0) and (peaks[k - 1] < peak) do
      begin
        sortIdx[k] := sortIdx[k - 1]; peaks[k] := peaks[k - 1]; Dec(k);
      end;
      sortIdx[k] := i; peaks[k] := peak;
    end;
    storiesArr := TJSONArray.Create;
    distinct := TStringList.Create;
    for k := 0 to High(sortIdx) do
    begin
      i := sortIdx[k];
      distinct.Clear;
      for j := 0 to High(RepStories[i].points) do
        if distinct.IndexOf(RepStories[i].points[j].day) < 0 then distinct.Add(RepStories[i].points[j].day);
      so := TJSONObject.Create;
      so.Add('id', RepStories[i].id);
      so.Add('title', RepStories[i].title);
      so.Add('category', RepStories[i].category);
      so.Add('topicId', RepStories[i].topicId);
      so.Add('firstSeen', RepStories[i].firstSeen);
      so.Add('lastSeen', RepStories[i].lastSeen);
      so.Add('peak', Rep_Num(peaks[k]));
      so.Add('editions', Length(RepStories[i].points));
      so.Add('days', distinct.Count);
      storiesArr.Add(so);
    end;
    distinct.Free;

    // ---- buildCosts: runs started within the days ----
    SetLength(socialD, count); SetLength(llmD, count);
    for d := 0 to count - 1 do begin socialD[d] := 0; llmD[d] := 0; end;
    tSocial := 0; tLlm := 0; tReq := 0; tCred := 0; tTok := 0;
    lo := Rep_StartOf(start);
    allRuns := DbLoad('runs');
    for i := 0 to allRuns.Count - 1 do
    begin
      run := allRuns.Items[i];
      pub := ParseIso(JStr(run, 'startedAt'));
      if (pub < lo) or (pub >= hi) then continue;
      d := days.IndexOf(ToDay(pub));
      if d < 0 then continue;
      w1 := 0; w2 := 0;
      if JPath(run, 'socialUsage.costUsd') <> nil then w1 := JNum(JObj(run, 'socialUsage'), 'costUsd');
      if JPath(run, 'usage.costUsd') <> nil then w2 := JNum(JObj(run, 'usage'), 'costUsd');
      socialD[d] := socialD[d] + w1;
      llmD[d] := llmD[d] + w2;
      tSocial := tSocial + w1;
      tLlm := tLlm + w2;
      tReq := tReq + JNum(JObj(run, 'socialUsage'), 'requests');
      tCred := tCred + JNum(JObj(run, 'socialUsage'), 'credits');
      tTok := tTok + JNum(JObj(run, 'usage'), 'inputTokens') + JNum(JObj(run, 'usage'), 'outputTokens');
    end;
    allRuns.Free;
    dailyArr := TJSONArray.Create;
    for d := 0 to count - 1 do
      dailyArr.Add(TJSONObject.Create(['day', days[d], 'social', Rep_Num(socialD[d]), 'llm', Rep_Num(llmD[d])]));
    ctot := TJSONObject.Create(['social', Rep_Num(tSocial), 'llm', Rep_Num(tLlm), 'total', Rep_Num(tSocial + tLlm),
      'requests', Rep_Num(tReq), 'credits', Rep_Num(tCred), 'tokens', Rep_Num(tTok)]);
    costs := TJSONObject.Create(['daily', dailyArr, 'totals', ctot]);

    // ---- assemble ----
    daysArr := StrsToJArr(days);
    cats := TJSONArray.Create;
    for i := 0 to High(REP_CATEGORIES) do cats.Add(REP_CATEGORIES[i]);
    totals := TJSONObject.Create(['editions', editionCount, 'topics', topicsTotal, 'stories', storiesTotal, 'newStories', newStories]);
    catDaily := TJSONObject.Create;
    for i := 0 to High(REP_CATEGORIES) do
    begin
      sts := TJSONArray.Create; scs := TJSONArray.Create;
      for d := 0 to count - 1 do
      begin
        sts.Add(Rep_Num(catStories[i][d]));
        scs.Add(Rep_Num(catScore[i][d]));
      end;
      catDaily.Add(REP_CATEGORIES[i], TJSONObject.Create(['stories', sts, 'score', scs]));
    end;
    outObj := TJSONObject.Create;
    outObj.Add('days', daysArr);
    outObj.Add('categories', cats);
    outObj.Add('totals', totals);
    outObj.Add('categoryDaily', catDaily);
    outObj.Add('dailyTop', dailyTop);
    outObj.Add('stories', storiesArr);
    outObj.Add('trends', trendsArr);
    outObj.Add('costs', costs);
    SendJson(200, outObj);

    // cleanup
    for i := 0 to High(eds) do
      for j := 0 to High(eds[i].topics) do eds[i].topics[j].itemIds.Free;
    for i := 0 to High(RepStories) do RepStories[i].items.Free;
    SetLength(RepStories, 0);
    days.Free;
    exit;
  end;
end;
