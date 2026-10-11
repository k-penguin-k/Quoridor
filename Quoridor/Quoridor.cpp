#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <string>
#include <utility>
#include <vector>

// ゲームルール
struct GameConfig {
	int board_size=9;
	int initial_walls=10;
};

// 盤面上のマスを表す座標
// col = 横方向、row = 縦方向
struct Position {
    int col = -1;
    int row = -1;
    bool operator==(const Position& other) const {
        return col == other.col && row == other.row;
    }
};

// 壁の向き
// Horizontal : 横壁
// Vertical : 縦壁
enum class WallDirection {
    Horizontal,
    Vertical
};

// 壁を1本置く位置
// position は壁の左上側にあるマスを基準とした座標
// direction によって横壁・縦壁を区別
struct WallPosition {
    Position position;
    WallDirection direction = WallDirection::Horizontal;
};

// ルール設定のどのボタンが押されたか
struct RuleButton {
    bool board_size_decrease = false;
    bool board_size_increase = false;
    bool walls_decrease = false;
    bool walls_increase = false;
    bool on_reset = false;
};

// ウィンドウサイズを管理
struct WindowSize {
    int width = 0;
    int height = 0;
};

// 描画に用いる値
struct Layout {
    int cell_size = 40;
    int wall_thickness = 10;
    int board_x = 10;
    int board_y = 100;
    int player_radius_ = 20;

    int info_column_posx = 500;
    int info_column_posy = 100;
    int info_column_width = 160;
    int info_column_height = 100;

    Layout() = default;

    int BoardWidth(const GameConfig& config) const {
        return wall_thickness * (config.board_size + 1) + cell_size * config.board_size;
    }
    int BoardHeight(const GameConfig& config) const {
        return BoardWidth(config);
    }

    // マスの左上の座標を求める
    int CellX(int col) const {
        return board_x + wall_thickness * (col + 1) + cell_size * col;
    }
    int CellY(int row) const {
        return board_y + wall_thickness * (row + 1) + cell_size * row;
    }
    
    // 盤面サイズに伴う情報欄の移動
    void Update(const GameConfig& config) {
        int board_width = BoardWidth(config);
        info_column_posx = board_x + board_width + 30;
        info_column_posy = board_y;
    }
};

// プレイヤーの状態を管理する。
class Player {
public:
    void SetPosition(Position pos) {
        position_ = pos;
    }

    void SetWall(int walls) {
        walls_remaining_ = walls;
    }

    // 壁を１本使用
    void UseWall() {
        if (walls_remaining_ > 0) {
            walls_remaining_--;
        }
    }

    Position GetPosition() const {
        return position_;
    }

    int GetWallsRemaining() const {
        return walls_remaining_;
    }

private:
    Position position_;
    int walls_remaining_= 10 ;
};

// マスの右・下の壁があるか
// 交差を防ぐためマスの右下の点も保持
struct Cell {
    bool right_wall_ = false;
    bool bottom_wall_ = false;
    bool right_bottom_point_ = false;
};

// 盤面のルール・壁・移動判定を管理
class Board {
public:
    explicit Board(int size = 9) {
        Resize(size);
    }

    // 盤面サイズを変更し、すべてのマス情報を初期化
    void Resize(int size) {
        board_size_ = std::max(2, size);
        cells_.assign(board_size_, std::vector<Cell>(board_size_));
    }

    int GetSize()const {
        return board_size_;
    }

    // 座標が現在の盤面内に存在するか
    bool IsInside(Position pos) const {
        return pos.col >= 0 && pos.col < board_size_ &&
            pos.row >= 0 && pos.row < board_size_;
    }

    // 上下左右の隣接マスか
    bool IsAdjacent(Position pos1, Position pos2) const {
        return (std::abs(pos1.col - pos2.col) + std::abs(pos1.row - pos2.row)) == 1;
    }

    // 2つのマスの間に壁が存在するか
    // 盤外を指定した場合は「壁がある」とみなす
    bool HasWallBetween(Position pos1, Position pos2) const {
        if (!IsInside(pos1) || !IsInside(pos2)) {
            return true;
        }
        // 上下方向
        if (pos1.col == pos2.col) {
            int min_row = std::min(pos1.row, pos2.row);
            return cells_[pos1.col][min_row].bottom_wall_;
        }
        // 左右方向
        if (pos1.row == pos2.row) {
            int min_col = std::min(pos1.col, pos2.col);
            return cells_[min_col][pos1.row].right_wall_;
        }
        // 隣接していない
        return false;
    }

    // プレイヤーが currentpos から newpos へ移動できるか
    bool CanMove(Position currentpos, Position opponentpos, Position newpos) const {
        if (!IsInside(newpos)) {
            return false;
        }
        if (newpos == opponentpos) {
            return false;
        }
        if (newpos == currentpos) {
            return false;
        }
        // 通常の１マス移動
        if (IsAdjacent(currentpos, newpos)) {
            if (!HasWallBetween(currentpos, newpos)) {
                return true;
            }
            return false;
        }
        // 相手を超えるジャンプ移動
        if (!IsAdjacent(currentpos, opponentpos)) {
            return false;
        }
        if (HasWallBetween(currentpos, opponentpos)) {
            return false;
        }
        if (!IsAdjacent(opponentpos, newpos)) {
            return false;
        }
        if (HasWallBetween(opponentpos, newpos)) {
            return false;
        }

        // 相手の真後ろの座標を計算
        int dx = opponentpos.col - currentpos.col;
        int dy = opponentpos.row - currentpos.row;
        Position behind_opponent = { opponentpos.col + dx, opponentpos.row + dy };
        
        // 相手の真後ろが移動先であれば、まっすぐジャンプ
        if (newpos == behind_opponent) {
            return true;
        }

        // まっすぐジャンプできないときの斜めジャンプ
        if (currentpos.col != newpos.col && currentpos.row != newpos.row) {
            // 相手の後ろが盤外であれば、ジャンプ可能
            if (!IsInside(behind_opponent)) {
                return true;
            }
            // 相手の後ろに壁があれば、ジャンプ可能
            if (HasWallBetween(opponentpos, behind_opponent)) {
                return true;
            }
        }
        return false;
    }

    // プレイヤーがゴールに到達したか
    // Player1は上端(row == 0)がゴール
    // Player2は下端(row == board_size - 1)がゴール
    bool CheckWin(const Player& player, bool player1) const {
        Position playerpos = player.GetPosition();
        if (player1) {
            return playerpos.row == 0;
        }
        else {
            return playerpos.row == board_size_ - 1;
        }
    }

    // 壁が置けるか
    bool CanPlaceWall(WallPosition wallpos, const Player& current_player) {
        // 壁が残っているか
        if (current_player.GetWallsRemaining() <= 0) {
            return false;
        }
        int col = wallpos.position.col;
        int row = wallpos.position.row;

        // 盤からはみ出しておけない
        if (col < 0 || col >= board_size_ - 1 ||
            row < 0 || row >= board_size_ - 1) {
            return false;
        }

        // 横壁の場合
        if (wallpos.direction == WallDirection::Horizontal) {
            if (cells_[col][row].bottom_wall_ ||
                cells_[col + 1][row].bottom_wall_ ||
                cells_[col][row].right_bottom_point_) {
                return false;
            }
            return true;
        }

        // 縦壁の場合
        if (wallpos.direction == WallDirection::Vertical) {
            if (cells_[col][row].right_wall_ ||
                cells_[col][row + 1].right_wall_ ||
                cells_[col][row].right_bottom_point_) {
                return false;
            }
            return true;
        }
        return false;
    }

    // ゴールまでの経路が残っているか
    bool CanReach(WallPosition wallpos, const Player& player, bool player1) {
        // 壁の仮配置
        PlaceWallInternal(wallpos, true);

        // BFS用の訪問済み配列
        std::vector<std::vector<bool>> visited(
            board_size_, std::vector<bool>(board_size_, false));

        std::queue<Position> q;

        // ゴールの設定
        int goalrow;
        if (player1) {
            goalrow = 0;
        }
        else {
            goalrow = board_size_ - 1;
        }

        // ゴール側全マスをスタート地点としてBFS
        for (int col = 0; col < board_size_; col++) {
            visited[col][goalrow] = true;
            q.push({col, goalrow});
        }

        bool reachable = false;

        while (!q.empty()) {
            Position current = q.front();
            q.pop();
            // 現在のプレイヤー位置に到達すれば、経路が存在
            if (current == player.GetPosition()) {
                reachable = true;
                break;
            }
            // 上下左右4方向を探索
            for (auto [dcol, drow] : std::vector<std::pair<int, int>>{ {1,0},{-1,0},{0,1},{0,-1} }) {
                Position next = { current.col + dcol,current.row + drow };
                if (!IsInside(next)) {
                    continue;
                }
                if (visited[next.col][next.row]) {
                    continue;
                }
                if (HasWallBetween(current, next)) {
                    continue;
                }
                visited[next.col][next.row] = true;
                q.push(next);
            }
        }
        // 仮配置した壁を消す
        PlaceWallInternal(wallpos, false);
        return reachable;
    }

    // 壁を配置
    void PlaceWall(WallPosition wallPos) {
        PlaceWallInternal(wallPos, true);
    }

    // Rendererが壁の状態を参照
    const Cell& GetCell(int col, int row) const {
        return cells_[col][row];
    }

private:
    // 壁を配置・削除
    // place == true : 壁を配置
    // place == false : 壁を削除
    void PlaceWallInternal(WallPosition wallPos, bool place) {
        int col = wallPos.position.col;
        int row = wallPos.position.row;

        if (wallPos.direction == WallDirection::Horizontal) {
            cells_[col][row].bottom_wall_ = place;
            cells_[col + 1][row].bottom_wall_ = place;
            cells_[col][row].right_bottom_point_ = place;
        }

        else if (wallPos.direction == WallDirection::Vertical) {
            cells_[col][row].right_wall_ = place;
            cells_[col][row + 1].right_wall_ = place;
            cells_[col][row].right_bottom_point_ = place;
        }
    }
    int board_size_ = 9;

    // cells_[col][row]の形式で盤面を管理
    std::vector<std::vector<Cell>> cells_;
};

// 描画
class Renderer {
public:
    
    explicit Renderer(
        const Layout& layout
    )
        : layout_(layout) {
    }

    void DrawBackground(const GameConfig& config) {
        int boardWidth = layout_.BoardWidth(config);
        int boardHeight = layout_.BoardHeight(config);

        DrawRectangle(layout_.board_x, layout_.board_y,
            boardWidth, boardHeight, DARKBROWN);

        DrawRectangle(layout_.board_x + layout_.wall_thickness,
            layout_.board_y + layout_.wall_thickness,
            layout_.wall_thickness * (config.board_size - 1) + layout_.cell_size * config.board_size,
            layout_.wall_thickness * (config.board_size - 1) + layout_.cell_size * config.board_size,
            GRAY);
    }

    void DrawBoard(const GameConfig& config) {
        for (int col = 0; col < config.board_size; col++) {
            for (int row = 0;row < config.board_size; row++) {
                DrawRectangle(layout_.CellX(col), layout_.CellY(row),
                    layout_.cell_size, layout_.cell_size,
                    LIGHTGRAY);
            }
        }
    }

    void DrawPlayer(const Player& player1, const Player& player2) {
        DrawPlayerOne(player1, RED);
        DrawPlayerOne(player2, BLUE);
    }

    void DrawWall(const Board& board) {
        int board_size = board.GetSize();
        for (int col = 0;col < board_size;col++) {
            for (int row = 0;row < board_size; row++) {
                const Cell& cell = board.GetCell(col, row);
                if (cell.right_wall_) {
                    int x = layout_.board_x
                            + layout_.wall_thickness * (col + 1)
                            + layout_.cell_size * (col + 1);
                    int y = layout_.board_y
                            + layout_.wall_thickness * (row + 1)
                            + layout_.cell_size * row;
                    DrawRectangle(x, y, layout_.wall_thickness, layout_.cell_size, DARKBROWN);
                }
                if (cell.bottom_wall_) {
                    int x = layout_.board_x 
                            + layout_.wall_thickness * (col + 1)
                            + layout_.cell_size * col;
                    int y = layout_.board_y
                            + layout_.wall_thickness * (row + 1)
                            + layout_.cell_size * (row + 1);
                    DrawRectangle(x, y, layout_.cell_size, layout_.wall_thickness, DARKBROWN);
                }
                if (cell.right_bottom_point_) {
                    int x = layout_.board_x
                            + layout_.wall_thickness * (col + 1)
                            + layout_.cell_size * (col + 1);
                    int y = layout_.board_y
                            + layout_.wall_thickness * (row + 1)
                            + layout_.cell_size * (row + 1);
                    DrawRectangle(x, y, layout_.wall_thickness, layout_.wall_thickness, DARKBROWN);
                }
            }
        }
    }

    // マウスがどのマス上にあるかを表示
    void DrawMouseCell(Position boardpos, bool player1_turn) {
        if (boardpos.col == -1 && boardpos.row == -1) {
            return;
        }
        int x = layout_.CellX(boardpos.col);
        int y = layout_.CellY(boardpos.row);

        if (player1_turn) {
            DrawRectangle(x, y, layout_.cell_size, layout_.cell_size, PINK);
        }
        else {
            DrawRectangle(x, y, layout_.cell_size, layout_.cell_size, SKYBLUE);
        }
    };

    // マウスがどの壁上にあるかを表示
    void DrawMouseWall(WallPosition wallPos, bool player1_turn) {
        if (wallPos.position.col == -1 && wallPos.position.row == -1) {
            return;
        }
        Color color;
        if (player1_turn) {
            color = PINK;
        }
        else {
            color = SKYBLUE;
        }
        if (wallPos.direction == WallDirection::Horizontal) {
            int x = layout_.board_x
                    + layout_.wall_thickness * (wallPos.position.col + 1)
                    + layout_.cell_size * wallPos.position.col;
            int y = layout_.board_y
                    + layout_.wall_thickness * (wallPos.position.row + 1)
                    + layout_.cell_size * (wallPos.position.row + 1);
            DrawRectangle(x, y,
                layout_.cell_size * 2 + layout_.wall_thickness,
                layout_.wall_thickness,
                color);
        }
        if (wallPos.direction == WallDirection::Vertical) {
            int x = layout_.board_x
                    + layout_.wall_thickness * (wallPos.position.col + 1)
                    + layout_.cell_size * (wallPos.position.col + 1);
            int y = layout_.board_y
                    + layout_.wall_thickness * (wallPos.position.row + 1)
                    + layout_.cell_size * wallPos.position.row;
            DrawRectangle(x, y,
                layout_.wall_thickness,
                layout_.cell_size * 2 + layout_.wall_thickness,
                color);
        }
    }

    // プレイヤー情報・残り壁数・現在のターンを表示
    void DrawInfoColumn(const Player& player1, const Player& player2, const bool player1_turn) {
        int x = layout_.info_column_posx;
        int y = layout_.info_column_posy;

        // 現在プレイヤーに枠を表示
        if (player1_turn) {
            DrawRectangle(x, y + 130,
                layout_.info_column_width, 100,
                RED);
        }
        else {
            DrawRectangle(x, y,
                layout_.info_column_width, 100,
                BLUE);
        }

        DrawRectangle(x + 10, y + 10,
            layout_.info_column_width - 20, layout_.info_column_height - 20,
            WHITE);
        DrawRectangle(x + 10, y + 140,
            layout_.info_column_width - 20, layout_.info_column_height - 20,
            WHITE);
        DrawText(("Walls: " + std::to_string(player1.GetWallsRemaining())).c_str(),
            x + 15, y + 190,
            22, RED);
        DrawText(("Walls: " + std::to_string(player2.GetWallsRemaining())).c_str(),
            x + 15, y + 60,
            22, BLUE);

        if (player1_turn) {
            DrawText("YOUR TURN",
                x + 15, y + 150,
                22, RED);
        }
        else {
            DrawText("YOUR TURN",
                x + 15, y + 20,
                22, BLUE);
        }
    }

    // 盤面サイズ・壁数・リセットボタンを表示する。
    void DrawRuleColumn(const GameConfig& config) {
        // 盤面サイズ
        DrawText("BoardSize", 10, 0, 20, BLACK);
        DrawRectangle(10, 25, 30, 30, BLACK);
        DrawRectangle(80, 25, 30, 30, BLACK);
        DrawText("-", 17, 27, 30, WHITE);
        DrawText("+", 87, 27, 30, WHITE);
        DrawText(std::to_string(config.board_size).c_str(), 50, 28, 20, BLACK);

        // 初期壁数
        DrawText( "Walls", 130, 0, 20, BLACK);
        DrawRectangle(130, 25, 30, 30, BLACK);
        DrawRectangle(200, 25, 30, 30, BLACK);
        DrawText("-", 137, 27, 30, WHITE);
        DrawText("+", 207, 27, 30, WHITE);
        DrawText(std::to_string(config.initial_walls).c_str(), 170, 28, 20, BLACK);

        // リセットボタン
        DrawRectangle(250, 10, 75, 45, BLACK);
        DrawText("RESET", 253, 25, 20, WHITE);
    }

    // 勝者を表示
    void DrawEnd(const bool player1_win) {
        if (player1_win) {
            DrawText("RED WIN",
                layout_.info_column_posx,
                layout_.info_column_posy + 240,
                30, RED);
        }
        else {
            DrawText("BLUE WIN",
                layout_.info_column_posx,
                layout_.info_column_posy + 240,
                30, BLUE);
        }
    }

private:
    // Playerの座標を画面上の円の中心座標へ変換
    void DrawPlayerOne(const Player& player, Color color) {
        Position pos = player.GetPosition();
        int x = layout_.CellX(pos.col) + layout_.cell_size / 2;
        int y = layout_.CellY(pos.row) + layout_.cell_size / 2;
        DrawCircle(x, y, layout_.player_radius_, color);
    }
    const Layout& layout_;
};

// ゲーム全体を管理
// 入力・ルール判定・プレイヤー状態・描画への橋渡し
class Game {
public:

    Game()
        : board_(config_.board_size),
        renderer_(layout_) {
        // 初期設定を適用
        applied_config_ = config_;
        layout_.Update(
            applied_config_
        );
    }
    
    // ゲーム開始時の初期化
    void Start() {
        ApplyConfigAndReset();
        InitWindow(
            window_size.width,
            window_size.height,
            "Quoridor"
        );
    }

    // 画面上のマウス座標を盤面座標へ変換
    Position ScreenToBoard(Vector2 mousePos) const {
        int board_size= applied_config_.board_size;
        for (int col = 0; col < board_size; col++) {
            for (int row = 0; row < board_size; row++) {
                int x = layout_.CellX(col);
                int y = layout_.CellY(row);
                if (mousePos.x >= x && mousePos.x < x + layout_.cell_size &&
                    mousePos.y >= y && mousePos.y < y + layout_.cell_size) {
                    return {col, row};
                }
            }
        }
        // 盤面外
        return {-1, -1};
    }

    // マウス座標を壁の位置へ変換
    WallPosition ScreenToWall(Vector2 mousePos) const {
        int boardSize = applied_config_.board_size;
        for (int col = 0; col < boardSize - 1; col++) {
            for (int row = 0; row < boardSize - 1; row++) {
                int x = layout_.board_x
                        + layout_.wall_thickness * (col + 1)
                        + layout_.cell_size * (col + 1);
                int y = layout_.board_y
                        + layout_.wall_thickness * (row + 1)
                        + layout_.cell_size * row;
                if (mousePos.x >= x && mousePos.x < x + layout_.wall_thickness &&
                    mousePos.y >= y && mousePos.y < y + layout_.cell_size * 2 + layout_.wall_thickness) {
                    return {{ col, row }, WallDirection::Vertical};
                }
            }
        }

        for (int col = 0; col < boardSize - 1; col++) {
            for (int row = 0; row < boardSize - 1; row++) {
                int x = layout_.board_x
                        + layout_.wall_thickness * (col + 1)
                        + layout_.cell_size *  col;
                int y = layout_.board_y
                        + layout_.wall_thickness * (row + 1)
                        + layout_.cell_size * (row + 1);
                if (mousePos.x >= x && mousePos.x < x + layout_.cell_size * 2 + layout_.wall_thickness &&
                    mousePos.y >= y && mousePos.y < y + layout_.wall_thickness) {
                    return {{ col, row }, WallDirection::Horizontal};
                }
            }
        }
        // 範囲外
        return {{ -1, -1 },WallDirection::Horizontal};
    }

    // マウス座標がどのルール設定ボタンに対応しているか
    RuleButton OnRuleButton(Vector2 mousepos) const {
        bool board_size_decrease = false;
        bool board_size_increase = false;
        bool walls_decrease = false;
        bool walls_increase = false;
        bool on_reset = false;

        if (mousepos.x >= 10 && mousepos.x <= 40 &&
            mousepos.y >= 25 && mousepos.y <= 55) {
            board_size_decrease = true;
        }
        if (mousepos.x >= 80 && mousepos.x <= 110 &&
            mousepos.y >= 25 && mousepos.y <= 55) {
            board_size_increase = true;
        }
        if (mousepos.x >= 130 && mousepos.x <= 160 &&
            mousepos.y >= 25 && mousepos.y <= 55) {
            walls_decrease = true;
        }
        if (mousepos.x >= 200 && mousepos.x <= 230 &&
            mousepos.y >= 25 && mousepos.y <= 55) {
            walls_increase = true;
        }
        if (mousepos.x >= 250 && mousepos.x <= 325 &&
            mousepos.y >= 10 && mousepos.y <= 55) {
            on_reset = true;
        }
        return {board_size_decrease, board_size_increase, walls_decrease, walls_increase, on_reset };
    }

    // 移動モードと壁配置モードを切り替える
    void ChangeMode() {
        if (!player1_win && !player2_win) {
            move_mode = !move_mode;
        }
    }

    // 現在のモードに応じてゲーム状態を更新
    void Update(Position boardpos, WallPosition wallpos) {
        if (player1_win || player2_win) {
            return;
        }
        if (move_mode) {
            UpdatePlayerPos(boardpos);
        }
        else {
            PlaceWall(wallpos);
        }
    }

    // プレイヤーの移動処理
    void UpdatePlayerPos(Position newpos) {
        if (player1_turn) {
            Position currentpos = player1_.GetPosition();
            Position opponentpos = player2_.GetPosition();
            // 移動可能か判定
            if (board_.CanMove(currentpos, opponentpos, newpos)) {
                player1_.SetPosition(newpos);
                // ゴールに到達したらゲーム終了
                if (board_.CheckWin(player1_, true)) {
                    player1_win = true;
                    return;
                }
                // Player2のターンへ
                player1_turn = false;
            }
        }
        else {
            Position currentpos = player2_.GetPosition();
            Position opponentpos = player1_.GetPosition();
            if (board_.CanMove(currentpos, opponentpos, newpos)) {
                player2_.SetPosition(newpos);
                if (board_.CheckWin(player2_, false)) {
                    player2_win = true;
                    return;
                }
                // Player1のターンへ
                player1_turn = true;
            }
        }
    }

    // 壁を配置
    void PlaceWall(WallPosition wallpos) {
        if (player1_turn) {
            // 壁が置けるか
            if (!board_.CanPlaceWall(wallpos, player1_)) {
                return;
            }
            // ゴールに到達できるか
            if (!board_.CanReach(wallpos, player1_, true) ||
                !board_.CanReach(wallpos, player2_, false)) {
                return;
            }
            // 実際に配置
            board_.PlaceWall(wallpos);
            player1_.UseWall();
            player1_turn = !player1_turn;            
        }
        else {
            if (!board_.CanPlaceWall(wallpos, player2_)) {
                return;
            }
            if (!board_.CanReach(wallpos, player1_, true) ||
                !board_.CanReach(wallpos, player2_, false)) {
                return;
            }
            board_.PlaceWall(wallpos);
            player2_.UseWall();
            player1_turn = !player1_turn;
        }
    }

    // ルール設定画面の変更を受け取る
    void RuleChange(RuleButton button) {
		if(button.board_size_decrease && config_.board_size > 3) {
			config_.board_size--;
		}
		if(button.board_size_increase && config_.board_size < 15) {
			config_.board_size++;
		}
        if (button.walls_decrease && config_.initial_walls > 0) {
			config_.initial_walls--;
        }
        if (button.walls_increase && config_.initial_walls < 99) {
            config_.initial_walls++;
        }
    }
    // RESETボタンが押された場合、設定を盤面へ適用してゲームを最初からやり直す
	void Reset(RuleButton button) {
		if (!button.on_reset) {
            return;
		}
        ApplyConfigAndReset();
        // 新しい盤面サイズに合わせてウィンドウサイズも変更
        SetWindowSize(
            window_size.width,
            window_size.height
        );
	}

    // 描画
    void Render(Position boardpos, WallPosition wallpos, RuleButton button) {
        renderer_.DrawBackground(applied_config_);
        renderer_.DrawBoard(applied_config_);
        if (move_mode) {
            renderer_.DrawMouseCell(boardpos, player1_turn);
        }
        else {
            renderer_.DrawMouseWall(wallpos, player1_turn);
        }
        renderer_.DrawPlayer(player1_, player2_);
        renderer_.DrawWall(board_);
        renderer_.DrawInfoColumn(player1_, player2_, player1_turn);
        renderer_.DrawRuleColumn(config_);
        if (player1_win) {
            renderer_.DrawEnd(true);
        }
        if (player2_win) {
            renderer_.DrawEnd(false);
        }
    }

private:
    // 現在のconfig_を実際のゲーム状態へ適用
    void ApplyConfigAndReset() {

        // UI上で変更された設定を実際に適用
        applied_config_ = config_;

        // 新しい盤面サイズに合わせてレイアウトを更新
        layout_.Update(applied_config_);

        // 盤面を作り直す
        board_.Resize(applied_config_.board_size);

        // ゲーム状態を初期化
        player1_.SetPosition({ applied_config_.board_size / 2, applied_config_.board_size - 1 });
        player1_.SetWall(applied_config_.initial_walls);

        player2_.SetPosition({applied_config_.board_size / 2, 0});
        player2_.SetWall(applied_config_.initial_walls);

        player1_turn = true;
        move_mode = true;
        player1_win = false;
        player2_win = false;

        // ウィンドウサイズを計算
        window_size.width = layout_.board_x
                            + layout_.BoardWidth(applied_config_)
                            + 30
                            + layout_.info_column_width
                            + 10;

        window_size.height =layout_.board_y
                            + layout_.BoardHeight(applied_config_)
                            + 120;
    }
    // UI上で現在設定されているルール
    GameConfig config_;
    // RESETによって実際のゲームへ適用されたルール
    GameConfig applied_config_;

    Layout layout_;
    Board board_;
    Player player1_;
    Player player2_;
    Renderer renderer_;
    
    // true : 移動モード
    // false : 壁配置モード
    bool move_mode = true;
    // trueならPlayer1、falseならPlayer2のターン
    bool player1_turn = true;
    bool player1_win = false;
    bool player2_win = false;

    WindowSize window_size;
};

int main() {

    Game game;

    // ウィンドウとゲーム状態を初期化
    game.Start();

    // ゲームループ
    while (!WindowShouldClose()) {
        // マウス位置を取得
        Vector2 mousepos = GetMousePosition();
        //ゲーム内座標に変換
        Position boardpos = game.ScreenToBoard(mousepos);
        WallPosition wallpos = game.ScreenToWall(mousepos);
        // ルール設定ボタンの判定
        RuleButton rule_button = game.OnRuleButton(mousepos);

        // 右クリックで移動と壁配置を切り替え
        if (IsMouseButtonReleased(MOUSE_RIGHT_BUTTON)) {
            game.ChangeMode();
        }

        // 左クリックでゲーム操作
        if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
            if (rule_button.on_reset) {
                game.Reset(rule_button);
            }
            else {
                game.RuleChange(rule_button);
            }
            game.Update(boardpos, wallpos);
        }

        // 描画
        BeginDrawing();
        ClearBackground(LIGHTGRAY);

        game.Render(boardpos, wallpos, rule_button);

        EndDrawing();
    }

    // 終了
    CloseWindow();

    return 0;

}