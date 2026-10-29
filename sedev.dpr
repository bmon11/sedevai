program sedev;
// Сэдэв.мн in one program. Backend (api) + frontend (html pages) together.
// Everything is included textually, order matters (later files can call earlier ones).

{$mode delphi}{$H+}
{$codepage utf8}

uses
  {$IFDEF UNIX}cthreads, cwstring,{$ENDIF}
  Classes, SysUtils, DateUtils, Math, StrUtils, SyncObjs, Process, base64, RegExpr,
  fpjson, jsonparser, fphttpserver, httpdefs, httpprotocol, fpmimetypes,
  DOM, SAX_HTML, DOM_HTML, zstream;

{$I core.inc}
{$I libtext.inc}
{$I sources.inc}
{$I collector.inc}
{$I social.inc}
{$I auth.inc}
{$I settings.inc}
{$I lenses.inc}
{$I entities.inc}
{$I articles.inc}
{$I audio.inc}
{$I chat.inc}
{$I reports.inc}
{$I topics.inc}
{$I pipeline.inc}
{$I cli.inc}
{$I pages_reader.inc}
{$I pages_admin.inc}

type
  TSedevServer = class
    procedure OnReq(Sender: TObject; var ARequest: TFPHTTPConnectionRequest; var AResponse: TFPHTTPConnectionResponse);
  end;

  TSchedThread = class(TThread)
  protected
    procedure Execute; override;
  end;

procedure TSchedThread.Execute;
var
  lastMin: string;
  mn: TDateTime;
begin
  lastMin := '';
  while not Terminated do
  begin
    mn := IncHour(NowUtc, 8);
    if FormatDateTime('yyyymmddhhnn', mn) <> lastMin then
    begin
      lastMin := FormatDateTime('yyyymmddhhnn', mn);
      try
        Pipe_SchedulerTick(mn);
      except
        on E: Exception do Log('scheduler: ' + E.Message);
      end;
    end;
    Sleep(5000);
  end;
end;

procedure ServeStatic;
var
  fn, path, ext, ct: string;
begin
  path := ReqPath;
  if (Pos('..', path) > 0) or (path = '/') then exit;
  fn := 'public' + path;
  if FileExists(fn) then
  begin
    // no /etc/mime.types on macOS, so a hardcoded list (copy of the usual suspects)
    ext := LowerCase(ExtractFileExt(fn));
    if ext = '.css' then ct := 'text/css; charset=utf-8'
    else if (ext = '.js') or (ext = '.mjs') then ct := 'text/javascript; charset=utf-8'
    else if ext = '.json' then ct := 'application/json'
    else if ext = '.webmanifest' then ct := 'application/manifest+json'
    else if ext = '.svg' then ct := 'image/svg+xml'
    else if ext = '.png' then ct := 'image/png'
    else if (ext = '.jpg') or (ext = '.jpeg') then ct := 'image/jpeg'
    else if ext = '.webp' then ct := 'image/webp'
    else if ext = '.gif' then ct := 'image/gif'
    else if ext = '.ico' then ct := 'image/x-icon'
    else if ext = '.woff2' then ct := 'font/woff2'
    else if ext = '.woff' then ct := 'font/woff'
    else if ext = '.mp3' then ct := 'audio/mpeg'
    else if (ext = '.html') or (ext = '.htm') then ct := 'text/html; charset=utf-8'
    else if ext = '.txt' then ct := 'text/plain; charset=utf-8'
    else ct := 'application/octet-stream';
    SendRaw(200, ct, ReadFileStr(fn));
    Resp.SetCustomHeader('Cache-Control', 'public, max-age=3600');
  end;
end;

procedure TSedevServer.OnReq(Sender: TObject; var ARequest: TFPHTTPConnectionRequest; var AResponse: TFPHTTPConnectionResponse);
var
  uri: string;
  t0: QWord;
begin
  t0 := GetTickCount64;
  Req := ARequest;
  Resp := AResponse;
  Handled := False;
  CurUser := nil;
  ReqBody := nil;
  ReqMethod := UpperCase(ARequest.Method);
  uri := ARequest.URI;
  if Pos('?', uri) > 0 then uri := Copy(uri, 1, Pos('?', uri) - 1);
  ReqPath := uri;
  if PathParts = nil then PathParts := TStringList.Create;
  PathParts.Clear;
  PathParts.Delimiter := '/'; PathParts.StrictDelimiter := True;
  PathParts.DelimitedText := Copy(uri, 2, MaxInt);
  if (PathParts.Count > 0) and (PathParts[PathParts.Count - 1] = '') then PathParts.Delete(PathParts.Count - 1);
  if SlownessMs > 0 then Sleep(SlownessMs); // "moderately slow"
  try
    try
      if Trim(ARequest.Content) <> '' then ReqBody := JParse(ARequest.Content);
      if Route('GET', '/api/health') then SendJsonStr(200, '{"ok":true}');
      Routes_auth;
      Routes_topics;
      Routes_social;
      Routes_articles;
      Routes_entities;
      Routes_audio;
      Routes_chat;
      Routes_sources;
      Routes_lenses;
      Routes_pipeline;
      Routes_reports;
      Routes_settings;
      Routes_libtext;
      Routes_collector;
      if not Handled and (Copy(ReqPath, 1, 5) <> '/api/') then
      begin
        ServeStatic;
        Routes_pages_reader;
        Routes_pages_admin;
      end;
      if not Handled and (Copy(ReqPath, 1, 11) = '/api/admin/') then RequireAuth; // 401 before 404 like express
      if not Handled then
      begin
        if Copy(ReqPath, 1, 5) = '/api/' then SendErr(404, 'Not found')
        else SendRaw(404, 'text/html; charset=utf-8', '<!doctype html><meta charset="utf-8"><title>404</title><body style="font-family:sans-serif;padding:40px"><h1>404</h1><p>Хуудас олдсонгүй.</p><a href="/">Нүүр</a></body>');
      end;
    except
      on E: EApi do SendErr(E.Status, E.Message);
      on E: Exception do
      begin
        Log('Unhandled error ' + ReqMethod + ' ' + ReqPath + ': ' + E.ClassName + ' ' + E.Message);
        SendErr(500, 'Internal error');
      end;
    end;
  finally
    FreeAndNil(ReqBody);
    FreeAndNil(CurUser);
  end;
  Log(ReqMethod + ' ' + ReqPath + ' ' + IntToStr(Resp.Code) + ' ' + IntToStr(GetTickCount64 - t0) + 'ms');
end;

var
  Srv: TFPHttpServer;
  App: TSedevServer;
  Sched: TSchedThread;
  cmd: string;
begin
  DefaultSystemCodePage := CP_UTF8;
  DefaultFormatSettings.DecimalSeparator := '.';
  Randomize;
  RandSeed := RandSeed xor LongInt(GetProcessID * 7919) xor LongInt(GetTickCount64);
  SetJSONInstanceType(jitNumberFloat, TJsFloat);
  DbLock := TCriticalSection.Create;
  LoadEnv;
  DataDir := Env('DATA_DIR', 'data');
  SlownessMs := StrToIntDef(Env('SLOWNESS_MS', '120'), 120);
  cmd := ParamStr(1);
  if (cmd <> '') and (cmd <> 'serve') then
  begin
    if not Cli_Main(cmd) then WriteLn('Unknown command: ', cmd);
    Halt(0);
  end;
  if Env('JWT_SECRET') = '' then begin WriteLn('Invalid environment: JWT_SECRET required'); Halt(1); end;
  Pipe_Boot;
  if Env('RUN_SCHEDULER', 'true') = 'true' then Sched := TSchedThread.Create(False);
  App := TSedevServer.Create;
  Srv := TFPHttpServer.Create(nil);
  Srv.Port := StrToIntDef(Env('PORT', '4001'), 4001);
  Srv.Threaded := False; // one request at a time. moderately slow, on purpose
  Srv.OnRequest := App.OnReq;
  Log('Сэдэв listening on :' + IntToStr(Srv.Port));
  Srv.Active := True;
end.
